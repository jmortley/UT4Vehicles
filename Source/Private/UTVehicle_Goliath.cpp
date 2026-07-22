#include "UTVehicle_Goliath.h"
#include "UnrealTournament.h"
#include "UTProj_TankShell.h"
#include "UTVehicleComponent.h"
#include "UTVehicleMeshComponent.h"
#include "UTVehicleMovementTank.h"
#include "WheeledVehicleMovementComponent4W.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Sound/SoundBase.h"
#include "UnrealNetwork.h"

namespace
{
	FRotator AddUT3Spread(const FRotator& BaseAim, float Spread)
	{
		if (Spread <= 0.0f)
		{
			return BaseAim;
		}

		const FRotationMatrix AimMatrix(BaseAim);
		const FVector X = AimMatrix.GetScaledAxis(EAxis::X);
		const FVector Y = AimMatrix.GetScaledAxis(EAxis::Y);
		const FVector Z = AimMatrix.GetScaledAxis(EAxis::Z);
		const float RandY = FMath::FRand() - 0.5f;
		const float RandZ = FMath::Sqrt(FMath::Max(0.0f, 0.5f - FMath::Square(RandY))) *
			(FMath::FRand() - 0.5f);
		return (X + RandY * Spread * Y + RandZ * Spread * Z).Rotation();
	}

	FVector GetWheelRestingPositionInChassisSpace(const FWheelSetup& WheelSetup,
		const USkeletalMeshComponent* Mesh)
	{
		const UVehicleWheel* WheelDefaults = WheelSetup.WheelClass != nullptr
			? WheelSetup.WheelClass->GetDefaultObject<UVehicleWheel>() : nullptr;
		FVector RestingPosition =
			(WheelDefaults != nullptr ? WheelDefaults->Offset : FVector::ZeroVector) +
			WheelSetup.AdditionalOffset;

		if (Mesh == nullptr || Mesh->SkeletalMesh == nullptr || WheelSetup.BoneName == NAME_None)
		{
			return RestingPosition;
		}

		const FVector MeshScale = Mesh->GetRelativeTransform().GetScale3D();
		FVector BonePosition = Mesh->SkeletalMesh->GetComposedRefPoseMatrix(
			WheelSetup.BoneName).GetOrigin() * MeshScale;

		// Match UWheeledVehicleMovementComponent::GetWheelRestingPosition().
		// PhysX stores wheel centres relative to the root physics BODY, which is
		// not necessarily the skeletal mesh's root. The Goliath's Chassis body is
		// translated by roughly 97 uu, so treating mesh-space as chassis-space
		// leaves every tire above its maximum suspension reach.
		const FBodyInstance* ChassisBody = Mesh->GetBodyInstance();
		if (ChassisBody != nullptr && ChassisBody->BodySetup != nullptr)
		{
			const FMatrix RootBodyMatrix = Mesh->SkeletalMesh->GetComposedRefPoseMatrix(
				ChassisBody->BodySetup->BoneName);
			BonePosition = RootBodyMatrix.InverseTransformPosition(BonePosition);
		}

		return RestingPosition + BonePosition;
	}

	FVector GetWheelRestingPositionInMeshSpace(const FWheelSetup& WheelSetup,
		const USkeletalMeshComponent* Mesh)
	{
		const FVector ChassisSpacePosition =
			GetWheelRestingPositionInChassisSpace(WheelSetup, Mesh);
		if (Mesh == nullptr || Mesh->SkeletalMesh == nullptr)
		{
			return ChassisSpacePosition;
		}

		const FBodyInstance* ChassisBody = Mesh->GetBodyInstance();
		if (ChassisBody == nullptr || ChassisBody->BodySetup == nullptr)
		{
			return ChassisSpacePosition;
		}

		// SetActorLocation() moves the skeletal mesh component, while PhysX wheel
		// centres are relative to the Chassis body. Convert back through the same
		// root-body reference transform that the engine removes during setup.
		const FMatrix RootBodyMatrix = Mesh->SkeletalMesh->GetComposedRefPoseMatrix(
			ChassisBody->BodySetup->BoneName);
		return RootBodyMatrix.TransformPosition(ChassisSpacePosition);
	}

	void ConfigureGoliathDrive(UUTVehicleMovementTank* Movement, USkeletalMeshComponent* Mesh)
	{
		if (UUTVehicleMeshComponent* VehicleMesh = Cast<UUTVehicleMeshComponent>(Mesh))
		{
			VehicleMesh->WeaponYawBoneName = FName(TEXT("Object01"));
			VehicleMesh->WeaponPitchBoneName = FName(TEXT("Object09"));
		}

		if (Movement == nullptr)
		{
			return;
		}

		// PhysX 4.15 exposes only its four-wheel drive publicly. Use two contact
		// points per tread and keep their steer angles at zero; native chassis yaw
		// below supplies the differential/skid steering behavior.
		// PxVehicleDrive4W requires this exact slot order. UT3 names wheel 05 as
		// the front contact and wheel 02 as the rear contact.
		Movement->WheelSetups.SetNum(4);
		Movement->WheelSetups[0].WheelClass = UUTWheel_Goliath::StaticClass();
		Movement->WheelSetups[0].BoneName = FName(TEXT("wheel_LHS_05"));
		Movement->WheelSetups[1].WheelClass = UUTWheel_Goliath::StaticClass();
		Movement->WheelSetups[1].BoneName = FName(TEXT("wheel_RHS_05"));
		Movement->WheelSetups[2].WheelClass = UUTWheel_Goliath::StaticClass();
		Movement->WheelSetups[2].BoneName = FName(TEXT("wheel_LHS_02"));
		Movement->WheelSetups[3].WheelClass = UUTWheel_Goliath::StaticClass();
		Movement->WheelSetups[3].BoneName = FName(TEXT("wheel_RHS_02"));
		for (FWheelSetup& WheelSetup : Movement->WheelSetups)
		{
			WheelSetup.AdditionalOffset = FVector::ZeroVector;
		}
		Movement->WheelSetups[0].AdditionalOffset.Y = -20.0f;
		Movement->WheelSetups[1].AdditionalOffset.Y = 20.0f;
		Movement->WheelSetups[2].AdditionalOffset.Y = -20.0f;
		Movement->WheelSetups[3].AdditionalOffset.Y = 20.0f;

		// Imported UT3 wheel bones are animation pivots, not guaranteed PhysX rest
		// positions. Put every contact at one chassis-derived height, leaving ten
		// units of clearance below the chassis collision. This prevents a deeply
		// compressed suspension from applying a launch impulse on its first tick.
		if (Mesh != nullptr && Mesh->SkeletalMesh != nullptr)
		{
			float TargetRestHeight = -40.0f;
			FBodyInstance* ChassisBody = Mesh->GetBodyInstance();
			if (ChassisBody != nullptr && ChassisBody->BodySetup != nullptr)
			{
				const FBox ChassisBounds = ChassisBody->BodySetup->AggGeom.CalcAABB(
					FTransform(FQuat::Identity, FVector::ZeroVector,
						Mesh->GetRelativeTransform().GetScale3D()));
				if (ChassisBounds.IsValid)
				{
					TargetRestHeight = ChassisBounds.Min.Z + 30.0f - 10.0f;
				}
			}

			for (FWheelSetup& WheelSetup : Movement->WheelSetups)
			{
				const float CurrentRestHeight =
					GetWheelRestingPositionInChassisSpace(WheelSetup, Mesh).Z;
				WheelSetup.AdditionalOffset.Z += TargetRestHeight - CurrentRestHeight;
			}
		}

		Movement->Mass = 10000.0f;
		Movement->InertiaTensorScale = FVector(1.0f, 1.2f, 1.5f);
		Movement->bReverseAsBrake = true;
		Movement->AckermannAccuracy = 0.0f;

		Movement->EngineSetup.MaxRPM = 3000.0f;
		Movement->EngineSetup.MOI = 2.0f;
		Movement->EngineSetup.DampingRateFullThrottle = 0.1f;
		Movement->EngineSetup.DampingRateZeroThrottleClutchEngaged = 2.0f;
		Movement->EngineSetup.DampingRateZeroThrottleClutchDisengaged = 0.5f;
		FRichCurve* TorqueCurve = Movement->EngineSetup.TorqueCurve.GetRichCurve();
		TorqueCurve->Reset();
		TorqueCurve->AddKey(0.0f, 7800.0f);
		TorqueCurve->AddKey(2600.0f, 7800.0f);
		TorqueCurve->AddKey(3000.0f, 0.0f);

		Movement->TransmissionSetup.bUseGearAutoBox = false;
		Movement->TransmissionSetup.ClutchStrength = 20.0f;
		Movement->TransmissionSetup.FinalRatio = 2.0f;
		Movement->TransmissionSetup.ReverseGearRatio = -2.6f;
		Movement->TransmissionSetup.NeutralGearUpRatio = 0.15f;
		Movement->TransmissionSetup.GearSwitchTime = 0.0f;
		Movement->TransmissionSetup.GearAutoBoxLatency = 0.0f;
		Movement->TransmissionSetup.ForwardGears.SetNum(1);
		Movement->TransmissionSetup.ForwardGears[0].Ratio = 2.6f;
		Movement->TransmissionSetup.ForwardGears[0].DownRatio = 0.5f;
		Movement->TransmissionSetup.ForwardGears[0].UpRatio = 0.95f;

		Movement->DifferentialSetup.DifferentialType = EVehicleDifferential4W::LimitedSlip_4W;
		Movement->DifferentialSetup.FrontRearSplit = 0.5f;
		Movement->DifferentialSetup.FrontLeftRightSplit = 0.5f;
		Movement->DifferentialSetup.RearLeftRightSplit = 0.5f;
		Movement->DifferentialSetup.CentreBias = 1.3f;
		Movement->DifferentialSetup.FrontBias = 1.3f;
		Movement->DifferentialSetup.RearBias = 1.3f;

		FRichCurve* SteeringCurve = Movement->SteeringCurve.GetRichCurve();
		SteeringCurve->Reset();
		SteeringCurve->AddKey(0.0f, 1.0f);
		SteeringCurve->AddKey(70.0f, 1.0f);
	}
}

AUTVehicle_Goliath::AUTVehicle_Goliath(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UUTVehicleMovementTank>(
		AWheeledVehicle::VehicleMovementComponentName))
{
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshFinder(
		TEXT("/Game/RestrictedAssets/Proto/UT3_Vehicles/VH_Goliath/Meshes/SK_VH_Goliath"));
	if (MeshFinder.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(MeshFinder.Object);
	}
	// The stock imported physics asset contains turret, tread, and wheel bodies.
	// Disabling those bodies after initialization is too late for the Goliath:
	// they can already destabilize its bounds/PhysX state, launching an untouched
	// spawn outside the world before its parking lock can hold it. The imported
	// vehicle content includes this one-body Chassis asset specifically for
	// the native vehicle path, so select it before physics is initialized.
	static ConstructorHelpers::FObjectFinder<UPhysicsAsset> ChassisPhysicsFinder(
		TEXT("/Game/RestrictedAssets/Proto/UT3_Vehicles/VH_Goliath/Meshes/PA_Goliath_ChassisOnly"));
	if (ChassisPhysicsFinder.Succeeded())
	{
		// UE4.15's SetPhysicsAsset() tries to rebuild articulated physics when a
		// SkeletalMesh is already assigned and dereferences GetWorld(). CDO
		// construction has no world, so set the public override directly; normal
		// component initialization will consume it when physics is actually created.
		GetMesh()->PhysicsAssetOverride = ChassisPhysicsFinder.Object;
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BodyMaterialFinder(
		TEXT("/Game/RestrictedAssets/Proto/UT3_Vehicles/VH_Goliath/Materials/M_Goliath"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TreadMaterialFinder(
		TEXT("/Game/RestrictedAssets/Proto/UT3_Vehicles/VH_Goliath/Materials/M_Goliath_Tread"));
	if (BodyMaterialFinder.Succeeded())
	{
		GetMesh()->SetMaterial(0, BodyMaterialFinder.Object);
	}
	if (TreadMaterialFinder.Succeeded())
	{
		GetMesh()->SetMaterial(1, TreadMaterialFinder.Object);
		GetMesh()->SetMaterial(2, TreadMaterialFinder.Object);
	}

	if (VehicleComponent != nullptr)
	{
		VehicleComponent->Health = 900;
		VehicleComponent->HealthMax = 900;
		VehicleComponent->bLightArmor = false;
		VehicleComponent->EntryRadius = 550.0f;
	}

	if (SpringArm != nullptr)
	{
		SpringArm->TargetArmLength = 1600.0f;
		SpringArm->SetRelativeLocation(FVector(100.0f, 0.0f, 650.0f));
		SpringArm->SetRelativeRotation(FRotator(-8.0f, 0.0f, 0.0f));
		SpringArm->bUsePawnControlRotation = true;
		SpringArm->bInheritPitch = true;
		SpringArm->bInheritYaw = true;
		SpringArm->bInheritRoll = false;
		SpringArm->bEnableCameraLag = true;
		SpringArm->CameraLagSpeed = 7.0f;
	}
	if (Camera != nullptr)
	{
		Camera->SetFieldOfView(90.0f);
	}

	TankTurnAcceleration = 2.5f;
	TankPivotYawRate = 60.0f;
	TankMovingYawRate = 30.0f;
	InsideTrackTorqueFactor = 0.25f;
	TankShellClass = AUTProj_TankShell::StaticClass();
	CannonMuzzleSocket = FName(TEXT("TurretFireSocket"));
	CannonFireInterval = 2.5f;
	CannonAimRotationRate = 60.0f;
	CannonSpread = 0.015f;
	static ConstructorHelpers::FObjectFinder<UParticleSystem> CannonMuzzleEffectFinder(
		TEXT("/Game/RestrictedAssets/Weapons/Weapon_Effects/Particles/P_RocketLauncher_MF_01_3P"));
	CannonMuzzleEffect = CannonMuzzleEffectFinder.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> CannonFireSoundFinder(
		TEXT("/Game/Mogno/Vehicles/Goliath/SFX/A_Vehicle_Goliath_Fire01"));
	CannonFireSound = CannonFireSoundFinder.Object;
	bCannonFiring = false;
	bSpawnParkingLocked = false;
	NextCannonFireTime = -1000.0f;
	LastCannonAimSendTime = -1000.0f;
	ReplicatedCannonAim = FRotator::ZeroRotator;
	CurrentCannonAim = FRotator::ZeroRotator;
	LastSentCannonAim = FRotator::ZeroRotator;

	GetMesh()->BodyInstance.COMNudge = FVector(-20.0f, 0.0f, -30.0f);
	ConfigureGoliathDrive(CastChecked<UUTVehicleMovementTank>(GetVehicleMovementComponent()), GetMesh());
}

void AUTVehicle_Goliath::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	UUTVehicleMovementTank* Movement = Cast<UUTVehicleMovementTank>(GetVehicleMovementComponent());
	ConfigureGoliathDrive(Movement, GetMesh());
	GetMesh()->BodyInstance.COMNudge = FVector(-20.0f, 0.0f, -30.0f);
	GetMesh()->SetEnableGravity(true);
	// UE4.15 keeps the override fields protected. Apply the runtime limit through
	// its public API after the chassis body has been created. Keep this deliberately
	// low: a parked 10-ton tank must resolve overlap instead of being catapulted.
	GetMesh()->BodyInstance.SetMaxDepenetrationVelocity(100.0f);
	GetMesh()->SetAngularDamping(4.0f);
	GetMesh()->SetLinearDamping(0.2f);

	if (SpringArm != nullptr)
	{
		SpringArm->TargetArmLength = 1600.0f;
		SpringArm->SetRelativeLocation(FVector(100.0f, 0.0f, 650.0f));
		SpringArm->SetRelativeRotation(FRotator(-8.0f, 0.0f, 0.0f));
		SpringArm->bUsePawnControlRotation = true;
		SpringArm->bEnableCameraLag = true;
		SpringArm->CameraLagSpeed = 7.0f;
	}

	if (Movement != nullptr && Movement->IsRegistered())
	{
		Movement->RecreatePhysicsState();
	}

	FBodyInstance* ChassisBody = GetMesh()->GetBodyInstance();
	const FBox ChassisBounds = ChassisBody != nullptr && ChassisBody->BodySetup != nullptr
		? ChassisBody->BodySetup->AggGeom.CalcAABB(FTransform(FQuat::Identity,
			FVector::ZeroVector, GetMesh()->GetRelativeTransform().GetScale3D()))
		: FBox(ForceInit);
	UE_LOG(LogTemp, Warning, TEXT("[GoliathPhysics] Setup Physics=%d ChassisBone=%s ChassisZ=(%.2f..%.2f) ParkHeight=%.2f WheelOffsetZ=(%.2f,%.2f,%.2f,%.2f) WheelRestBodyZ=(%.2f,%.2f,%.2f,%.2f) WheelRestMeshZ=(%.2f,%.2f,%.2f,%.2f)"),
		Movement != nullptr && Movement->HasValidPhysicsState() ? 1 : 0,
		ChassisBody != nullptr && ChassisBody->BodySetup != nullptr
			? *ChassisBody->BodySetup->BoneName.ToString() : TEXT("NONE"),
		ChassisBounds.IsValid ? ChassisBounds.Min.Z : 0.0f,
		ChassisBounds.IsValid ? ChassisBounds.Max.Z : 0.0f,
		GetParkedHeightAboveGround(),
		Movement != nullptr && Movement->WheelSetups.IsValidIndex(0) ? Movement->WheelSetups[0].AdditionalOffset.Z : 0.0f,
		Movement != nullptr && Movement->WheelSetups.IsValidIndex(1) ? Movement->WheelSetups[1].AdditionalOffset.Z : 0.0f,
		Movement != nullptr && Movement->WheelSetups.IsValidIndex(2) ? Movement->WheelSetups[2].AdditionalOffset.Z : 0.0f,
		Movement != nullptr && Movement->WheelSetups.IsValidIndex(3) ? Movement->WheelSetups[3].AdditionalOffset.Z : 0.0f,
		Movement != nullptr && Movement->WheelSetups.IsValidIndex(0) ? GetWheelRestingPositionInChassisSpace(Movement->WheelSetups[0], GetMesh()).Z : 0.0f,
		Movement != nullptr && Movement->WheelSetups.IsValidIndex(1) ? GetWheelRestingPositionInChassisSpace(Movement->WheelSetups[1], GetMesh()).Z : 0.0f,
		Movement != nullptr && Movement->WheelSetups.IsValidIndex(2) ? GetWheelRestingPositionInChassisSpace(Movement->WheelSetups[2], GetMesh()).Z : 0.0f,
		Movement != nullptr && Movement->WheelSetups.IsValidIndex(3) ? GetWheelRestingPositionInChassisSpace(Movement->WheelSetups[3], GetMesh()).Z : 0.0f,
		Movement != nullptr && Movement->WheelSetups.IsValidIndex(0) ? GetWheelRestingPositionInMeshSpace(Movement->WheelSetups[0], GetMesh()).Z : 0.0f,
		Movement != nullptr && Movement->WheelSetups.IsValidIndex(1) ? GetWheelRestingPositionInMeshSpace(Movement->WheelSetups[1], GetMesh()).Z : 0.0f,
		Movement != nullptr && Movement->WheelSetups.IsValidIndex(2) ? GetWheelRestingPositionInMeshSpace(Movement->WheelSetups[2], GetMesh()).Z : 0.0f,
		Movement != nullptr && Movement->WheelSetups.IsValidIndex(3) ? GetWheelRestingPositionInMeshSpace(Movement->WheelSetups[3], GetMesh()).Z : 0.0f);
}

void AUTVehicle_Goliath::BeginPlay()
{
	Super::BeginPlay();

	// Spawners place every vehicle at a map-authored pad height. The Goliath's
	// imported pivot and chassis are much taller than the Scorpion's, so Z=60 can
	// start its wheels/chassis inside the floor. Correct that before the first
	// PhysX vehicle update rather than asking depenetration and four stiff springs
	// to eject a 10-ton overlap.
	if (VehicleComponent != nullptr && !VehicleComponent->HasDriver())
	{
		float GroundZ = 0.0f;
		float ParkedActorZ = 0.0f;
		if (FindParkingGround(GroundZ, ParkedActorZ))
		{
			FVector ParkedLocation = GetActorLocation();
			ParkedLocation.Z = ParkedActorZ;
			SetActorLocation(ParkedLocation, false, nullptr, ETeleportType::TeleportPhysics);
			GetMesh()->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
			GetMesh()->SetAllPhysicsAngularVelocity(FVector::ZeroVector);
			GetMesh()->PutAllRigidBodiesToSleep();
			if (UUTVehicleMovementTank* Movement = Cast<UUTVehicleMovementTank>(GetVehicleMovementComponent()))
			{
				// Sleeping is not a parking lock: PhysX may wake the body later and a
				// single bad suspension/depenetration step can cross the entire map
				// before the next actor tick. Disable physical response and gravity on
				// the untouched spawn while retaining query collision, so the nearby
				// entry prompt and RPC still see the tank. Keeping the body simulated
				// also preserves the already-created PhysX vehicle state for entry.
				Movement->Deactivate();
			}
			if (FBodyInstance* ChassisBody = GetMesh()->GetBodyInstance())
			{
				ChassisBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				GetMesh()->SetEnableGravity(false);
				bSpawnParkingLocked = true;
			}
			UE_LOG(LogTemp, Warning, TEXT("[GoliathParking] Spawn parked Vehicle=%s GroundZ=%.2f ActorZ=%.2f Height=%.2f"),
				*GetName(), GroundZ, ParkedActorZ, GetParkedHeightAboveGround());
		}
	}
}

void AUTVehicle_Goliath::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateCannonAim(DeltaSeconds);
	ApplyTankSteering();
	UpdateVacantParking(DeltaSeconds);
}

void AUTVehicle_Goliath::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AUTVehicle_Goliath, ReplicatedCannonAim, COND_SkipOwner);
}

FRotator AUTVehicle_Goliath::SanitizeCannonAim(const FRotator& AimRotation) const
{
	FRotator Result;
	Result.Pitch = FMath::Clamp(FRotator::NormalizeAxis(AimRotation.Pitch), -10.0f, 35.0f);
	Result.Yaw = FRotator::NormalizeAxis(AimRotation.Yaw);
	Result.Roll = 0.0f;
	return Result;
}

FRotator AUTVehicle_Goliath::GetDesiredCannonAimRotation() const
{
	const FRotator WorldAim = Controller != nullptr
		? Controller->GetControlRotation()
		: GetActorRotation();
	const USkeletalMeshComponent* Mesh = GetMesh();
	if (Mesh == nullptr)
	{
		return SanitizeCannonAim(WorldAim - GetActorRotation());
	}
	const FVector LocalAimDirection = Mesh->GetComponentTransform().InverseTransformVectorNoScale(
		WorldAim.Vector());
	return SanitizeCannonAim(LocalAimDirection.Rotation());
}

void AUTVehicle_Goliath::UpdateCannonAim(float DeltaSeconds)
{
	const bool bOccupied = VehicleComponent != nullptr && VehicleComponent->HasDriver() &&
		!VehicleComponent->bDead;
	if (bOccupied && IsLocallyControlled())
	{
		const FRotator DesiredAim = GetDesiredCannonAimRotation();
		ReplicatedCannonAim = DesiredAim;
		if (Role < ROLE_Authority && GetWorld() != nullptr)
		{
			const float Now = GetWorld()->GetTimeSeconds();
			const bool bAimChanged =
				FMath::Abs(FMath::FindDeltaAngleDegrees(LastSentCannonAim.Yaw, DesiredAim.Yaw)) >= 0.25f ||
				FMath::Abs(FMath::FindDeltaAngleDegrees(LastSentCannonAim.Pitch, DesiredAim.Pitch)) >= 0.25f;
			if (bAimChanged && Now - LastCannonAimSendTime >= 0.05f)
			{
				LastSentCannonAim = DesiredAim;
				LastCannonAimSendTime = Now;
				ServerSetCannonAim(DesiredAim);
			}
		}
	}

	CurrentCannonAim = FMath::RInterpConstantTo(CurrentCannonAim,
		SanitizeCannonAim(ReplicatedCannonAim), DeltaSeconds, CannonAimRotationRate);
	if (UUTVehicleMeshComponent* VehicleMesh = Cast<UUTVehicleMeshComponent>(GetMesh()))
	{
		VehicleMesh->SetWeaponAimRotation(CurrentCannonAim);
	}
}

bool AUTVehicle_Goliath::ServerSetCannonAim_Validate(FRotator NewAimRotation)
{
	return FMath::IsFinite(NewAimRotation.Pitch) && FMath::IsFinite(NewAimRotation.Yaw) &&
		FMath::IsFinite(NewAimRotation.Roll);
}

void AUTVehicle_Goliath::ServerSetCannonAim_Implementation(FRotator NewAimRotation)
{
	if (VehicleComponent != nullptr && VehicleComponent->HasDriver() && !VehicleComponent->bDead)
	{
		ReplicatedCannonAim = SanitizeCannonAim(NewAimRotation);
		ForceNetUpdate();
	}
}

void AUTVehicle_Goliath::OnRep_CannonAim()
{
	ReplicatedCannonAim = SanitizeCannonAim(ReplicatedCannonAim);
}

float AUTVehicle_Goliath::GetParkedHeightAboveGround() const
{
	const UUTVehicleMovementTank* Movement = Cast<UUTVehicleMovementTank>(GetVehicleMovementComponent());
	const USkeletalMeshComponent* Mesh = GetMesh();
	if (Movement == nullptr || Mesh == nullptr || Mesh->SkeletalMesh == nullptr ||
		!Movement->WheelSetups.IsValidIndex(0) || Movement->WheelSetups[0].WheelClass == nullptr)
	{
		return 100.0f;
	}

	const FWheelSetup& WheelSetup = Movement->WheelSetups[0];
	const float LocalWheelCenterZ =
		GetWheelRestingPositionInMeshSpace(WheelSetup, Mesh).Z;
	const UVehicleWheel* WheelDefaults = WheelSetup.WheelClass->GetDefaultObject<UVehicleWheel>();
	const float WheelRadius = WheelDefaults != nullptr ? WheelDefaults->ShapeRadius : 30.0f;
	// Two units keep the tire touching the floor without beginning in penetration.
	return FMath::Clamp(WheelRadius - LocalWheelCenterZ + 2.0f, 50.0f, 500.0f);
}

bool AUTVehicle_Goliath::FindParkingGround(float& OutGroundZ, float& OutParkedActorZ) const
{
	if (GetWorld() == nullptr)
	{
		return false;
	}

	const FVector ActorLocation = GetActorLocation();
	// Start just above the chassis pivot so an overhead bridge is never mistaken
	// for the parking surface beneath the tank.
	const FVector TraceStart = ActorLocation + FVector(0.0f, 0.0f, 50.0f);
	const FVector TraceEnd = ActorLocation - FVector(0.0f, 0.0f, 10000.0f);
	FCollisionQueryParams QueryParams(FName(TEXT("GoliathParkingGround")), false, this);
	FHitResult GroundHit;
	if (!GetWorld()->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd,
		ECC_WorldStatic, QueryParams) || GroundHit.ImpactNormal.Z < 0.5f)
	{
		return false;
	}

	OutGroundZ = GroundHit.ImpactPoint.Z;
	OutParkedActorZ = OutGroundZ + GetParkedHeightAboveGround();
	return true;
}

void AUTVehicle_Goliath::UpdateVacantParking(float DeltaSeconds)
{
	UUTVehicleMovementTank* Movement = Cast<UUTVehicleMovementTank>(GetVehicleMovementComponent());
	if (GetMesh() == nullptr || Movement == nullptr || VehicleComponent == nullptr ||
		VehicleComponent->bDead)
	{
		return;
	}
	if (VehicleComponent->HasDriver())
	{
		if (bSpawnParkingLocked)
		{
			// Restore only the chassis body's physical response. Never recreate the
			// skeletal or movement physics states here: doing so resurrects the
			// imported turret/tread bodies that AUTVehicle deliberately strips out.
			if (FBodyInstance* ChassisBody = GetMesh()->GetBodyInstance())
			{
				ChassisBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			}
			GetMesh()->SetEnableGravity(true);
			bSpawnParkingLocked = false;
			UE_LOG(LogTemp, Warning, TEXT("[GoliathParking] Spawn lock released Vehicle=%s Role=%d Physics=%d"),
				*GetName(), (int32)Role, GetMesh()->IsSimulatingPhysics() ? 1 : 0);
		}
		if (!Movement->IsActive())
		{
			Movement->Activate(true);
			GetMesh()->WakeAllRigidBodies();
			UE_LOG(LogTemp, Warning, TEXT("[GoliathParking] Drive enabled Vehicle=%s Role=%d"),
				*GetName(), (int32)Role);
		}
		return;
	}
	if (bSpawnParkingLocked)
	{
		// The rigid chassis cannot accumulate gravity, collision, suspension, or
		// replication wake-up impulses while this first-spawn lock is active.
		return;
	}

	// Run this on authority and clients. PhysX vehicles simulate on each world;
	// parking only the server still lets an empty simulated proxy visibly explode.
	if (Movement->IsActive())
	{
		Movement->Deactivate();
	}
	if (!GetMesh()->IsSimulatingPhysics())
	{
		return;
	}

	float GroundZ = 0.0f;
	float ParkedActorZ = 0.0f;
	if (!FindParkingGround(GroundZ, ParkedActorZ))
	{
		return;
	}

	FVector Velocity = GetMesh()->GetPhysicsLinearVelocity();
	// Never allow an empty tank's suspension/depenetration to create lift. If it
	// was exited in the air, gravity is still allowed to bring it down gradually.
	const FVector PlanarVelocity(Velocity.X, Velocity.Y, 0.0f);
	const FVector BrakedPlanarVelocity = FMath::VInterpConstantTo(PlanarVelocity,
		FVector::ZeroVector, DeltaSeconds, 2500.0f);
	Velocity.X = BrakedPlanarVelocity.X;
	Velocity.Y = BrakedPlanarVelocity.Y;
	Velocity.Z = FMath::Min(Velocity.Z, 0.0f);
	GetMesh()->SetAllPhysicsLinearVelocity(Velocity);

	// No drive/suspension tick is active while vacant, so there is no legitimate
	// angular input to preserve. Kill it outright on every network copy.
	GetMesh()->SetAllPhysicsAngularVelocity(FVector::ZeroVector);

	const float HeightError = GetActorLocation().Z - ParkedActorZ;
	if (HeightError < -2.0f)
	{
		// Repair residual floor penetration without imparting an impulse.
		FVector CorrectedLocation = GetActorLocation();
		CorrectedLocation.Z = ParkedActorZ;
		SetActorLocation(CorrectedLocation, false, nullptr, ETeleportType::TeleportPhysics);
		GetMesh()->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
		GetMesh()->SetAllPhysicsAngularVelocity(FVector::ZeroVector);
	}
	else if (FMath::Abs(HeightError) <= 25.0f && Velocity.SizeSquared() < FMath::Square(100.0f))
	{
		// Normalize the imported chassis to an upright, exact tire-contact pose.
		// Sleep is the actual parking lock; replicated driver state reactivates the
		// movement component and driving input wakes the body.
		FVector ParkedLocation = GetActorLocation();
		ParkedLocation.Z = ParkedActorZ;
		FRotator ParkedRotation = GetActorRotation();
		ParkedRotation.Pitch = 0.0f;
		ParkedRotation.Roll = 0.0f;
		SetActorLocationAndRotation(ParkedLocation, ParkedRotation, false, nullptr,
			ETeleportType::TeleportPhysics);
		GetMesh()->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
		GetMesh()->SetAllPhysicsAngularVelocity(FVector::ZeroVector);
		GetMesh()->PutAllRigidBodiesToSleep();
	}
}

void AUTVehicle_Goliath::ApplyTankSteering()
{
	if (Role != ROLE_Authority && !IsLocallyControlled())
	{
		return;
	}

	UUTVehicleMovementTank* Movement = Cast<UUTVehicleMovementTank>(GetVehicleMovementComponent());
	USkeletalMeshComponent* Mesh = GetMesh();
	if (Movement == nullptr || Mesh == nullptr || !Mesh->IsSimulatingPhysics() ||
		VehicleComponent == nullptr || !VehicleComponent->HasDriver() || VehicleComponent->bDead)
	{
		return;
	}

	const float Steering = Movement->GetAppliedTankSteering();
	if (FMath::Abs(Steering) <= 0.01f)
	{
		return;
	}

	const float SpeedAlpha = FMath::Clamp(FMath::Abs(Movement->GetForwardSpeed()) / 1800.0f, 0.0f, 1.0f);
	const float MaxYawRate = FMath::Lerp(TankPivotYawRate, TankMovingYawRate, SpeedAlpha);
	const float CurrentYawRate = Mesh->GetPhysicsAngularVelocity().Z;
	if (FMath::Abs(CurrentYawRate) < MaxYawRate || FMath::Sign(CurrentYawRate) != FMath::Sign(Steering))
	{
		const float MovingTurnScale = FMath::Lerp(1.0f, InsideTrackTorqueFactor, SpeedAlpha);
		Mesh->AddTorque(FVector::UpVector * Steering * TankTurnAcceleration * MovingTurnScale,
			NAME_None, true);
	}
}

void AUTVehicle_Goliath::OnPrimaryFirePressed()
{
	if (Role == ROLE_Authority)
	{
		ReplicatedCannonAim = GetDesiredCannonAimRotation();
		SetCannonFiring(true);
	}
	else
	{
		ServerSetCannonFiring(true, GetDesiredCannonAimRotation());
	}
}

void AUTVehicle_Goliath::OnPrimaryFireReleased()
{
	if (Role == ROLE_Authority)
	{
		ReplicatedCannonAim = GetDesiredCannonAimRotation();
		SetCannonFiring(false);
	}
	else
	{
		ServerSetCannonFiring(false, GetDesiredCannonAimRotation());
	}
}

bool AUTVehicle_Goliath::ServerSetCannonFiring_Validate(bool, FRotator NewAimRotation)
{
	return FMath::IsFinite(NewAimRotation.Pitch) && FMath::IsFinite(NewAimRotation.Yaw) &&
		FMath::IsFinite(NewAimRotation.Roll);
}

void AUTVehicle_Goliath::ServerSetCannonFiring_Implementation(bool bNewFiring,
	FRotator NewAimRotation)
{
	ReplicatedCannonAim = SanitizeCannonAim(NewAimRotation);
	SetCannonFiring(bNewFiring);
}

void AUTVehicle_Goliath::SetCannonFiring(bool bNewFiring)
{
	if (Role != ROLE_Authority)
	{
		return;
	}

	bCannonFiring = bNewFiring && VehicleComponent != nullptr &&
		VehicleComponent->HasDriver() && !VehicleComponent->bDead;
	if (GetWorld() != nullptr)
	{
		GetWorldTimerManager().ClearTimer(CannonFireTimerHandle);
	}
	if (bCannonFiring)
	{
		FireCannon();
	}
}

void AUTVehicle_Goliath::FireCannon()
{
	if (Role != ROLE_Authority || GetWorld() == nullptr || !bCannonFiring ||
		VehicleComponent == nullptr || !VehicleComponent->HasDriver() || VehicleComponent->bDead)
	{
		SetCannonFiring(false);
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextCannonFireTime)
	{
		GetWorldTimerManager().SetTimer(CannonFireTimerHandle, this,
			&AUTVehicle_Goliath::FireCannon, NextCannonFireTime - Now, false);
		return;
	}

	USkeletalMeshComponent* Mesh = GetMesh();
	FVector SpawnLocation = GetActorLocation() + GetActorForwardVector() * 350.0f + FVector(0.0f, 0.0f, 220.0f);
	if (Mesh != nullptr && Mesh->DoesSocketExist(CannonMuzzleSocket))
	{
		SpawnLocation = Mesh->GetSocketLocation(CannonMuzzleSocket);
	}
	const FRotator SpawnRotation = AddUT3Spread(GetCannonAimRotation(), CannonSpread);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.Instigator = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UClass* ProjectileClass = TankShellClass.Get() != nullptr
		? TankShellClass.Get()
		: AUTProj_TankShell::StaticClass();
	AUTProj_TankShell* Shell = GetWorld()->SpawnActor<AUTProj_TankShell>(
		ProjectileClass, SpawnLocation, SpawnRotation, SpawnParameters);
	if (Shell != nullptr)
	{
		Shell->InstigatorController = Controller != nullptr
			? Controller
			: VehicleComponent->DamageInstigator;
		Shell->InstigatorTeamNum = GetTeamNum();
		if (GetMesh() != nullptr)
		{
			// UT3 kicks the chassis with half the shell's launch velocity at a
			// raised point, giving the heavy cannon a small visible recoil.
			GetMesh()->AddImpulseAtLocation(-0.5f * Shell->GetVelocity(),
				GetActorLocation() + FVector(0.0f, 0.0f, 90.0f));
		}
		MulticastPlayCannonFire(SpawnLocation, SpawnRotation);
	}

	NextCannonFireTime = Now + CannonFireInterval;
	GetWorldTimerManager().SetTimer(CannonFireTimerHandle, this,
		&AUTVehicle_Goliath::FireCannon, CannonFireInterval, false);
}

FRotator AUTVehicle_Goliath::GetCannonAimRotation() const
{
	const USkeletalMeshComponent* Mesh = GetMesh();
	const FRotator ProjectileAim = SanitizeCannonAim(ReplicatedCannonAim);
	const FVector WorldAimDirection = Mesh != nullptr
		? Mesh->GetComponentTransform().TransformVectorNoScale(ProjectileAim.Vector())
		: GetActorTransform().TransformVectorNoScale(ProjectileAim.Vector());
	return WorldAimDirection.Rotation();
}

void AUTVehicle_Goliath::MulticastPlayCannonFire_Implementation(
	FVector MuzzleLocation, FRotator MuzzleRotation)
{
	if (GetWorld() == nullptr || GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (CannonMuzzleEffect != nullptr)
	{
		UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), CannonMuzzleEffect,
			MuzzleLocation, MuzzleRotation, true);
	}
	if (CannonFireSound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(this, CannonFireSound, MuzzleLocation,
			1.0f, 1.0f, 0.0f, VehicleSoundAttenuation);
	}
}

bool AUTVehicle_Goliath::HandleDriverLeaveRequest()
{
	if (Role == ROLE_Authority)
	{
		SetCannonFiring(false);
	}
	return Super::HandleDriverLeaveRequest();
}

void AUTVehicle_Goliath::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bCannonFiring = false;
	if (GetWorld() != nullptr)
	{
		GetWorldTimerManager().ClearTimer(CannonFireTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

UUTWheel_Goliath::UUTWheel_Goliath()
{
	ShapeRadius = 30.0f;
	ShapeWidth = 45.0f;
	Mass = 80.0f;
	DampingRate = 0.5f;
	SteerAngle = 0.0f;
	bAffectedByHandbrake = true;

	LatStiffMaxLoad = 3.0f;
	LatStiffValue = 40.0f;
	LongStiffValue = 3000.0f;

	SuspensionForceOffset = -10.0f;
	SuspensionMaxRaise = 45.0f;
	SuspensionMaxDrop = 45.0f;
	SuspensionNaturalFrequency = 8.0f;
	SuspensionDampingRatio = 1.5f;

	MaxBrakeTorque = 10000.0f;
	MaxHandBrakeTorque = 20000.0f;
}
