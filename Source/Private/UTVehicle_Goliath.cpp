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

namespace
{
	void ConfigureGoliathDrive(UUTVehicleMovementTank* Movement)
	{
		if (Movement == nullptr)
		{
			return;
		}

		// PhysX 4.15 exposes only its four-wheel drive publicly. Use two contact
		// points per tread and keep their steer angles at zero; native chassis yaw
		// below supplies the differential/skid steering behavior.
		Movement->WheelSetups.SetNum(4);
		Movement->WheelSetups[0].WheelClass = UUTWheel_Goliath::StaticClass();
		Movement->WheelSetups[0].BoneName = FName(TEXT("wheel_LHS_02"));
		Movement->WheelSetups[1].WheelClass = UUTWheel_Goliath::StaticClass();
		Movement->WheelSetups[1].BoneName = FName(TEXT("wheel_RHS_02"));
		Movement->WheelSetups[2].WheelClass = UUTWheel_Goliath::StaticClass();
		Movement->WheelSetups[2].BoneName = FName(TEXT("wheel_LHS_05"));
		Movement->WheelSetups[3].WheelClass = UUTWheel_Goliath::StaticClass();
		Movement->WheelSetups[3].BoneName = FName(TEXT("wheel_RHS_05"));
		for (FWheelSetup& WheelSetup : Movement->WheelSetups)
		{
			WheelSetup.AdditionalOffset = FVector::ZeroVector;
		}
		Movement->WheelSetups[0].AdditionalOffset.Y = -20.0f;
		Movement->WheelSetups[1].AdditionalOffset.Y = 20.0f;
		Movement->WheelSetups[2].AdditionalOffset.Y = -20.0f;
		Movement->WheelSetups[3].AdditionalOffset.Y = 20.0f;

		Movement->Mass = 10000.0f;
		Movement->InertiaTensorScale = FVector(1.0f, 1.2f, 1.5f);
		Movement->bReverseAsBrake = true;

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
	bCannonFiring = false;
	NextCannonFireTime = -1000.0f;

	GetMesh()->BodyInstance.COMNudge = FVector(-20.0f, 0.0f, -30.0f);
	ConfigureGoliathDrive(CastChecked<UUTVehicleMovementTank>(GetVehicleMovementComponent()));
}

void AUTVehicle_Goliath::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	UUTVehicleMovementTank* Movement = Cast<UUTVehicleMovementTank>(GetVehicleMovementComponent());
	ConfigureGoliathDrive(Movement);
	GetMesh()->BodyInstance.COMNudge = FVector(-20.0f, 0.0f, -30.0f);
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
}

void AUTVehicle_Goliath::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ApplyTankSteering();
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
		SetCannonFiring(true);
	}
	else
	{
		ServerSetCannonFiring(true);
	}
}

void AUTVehicle_Goliath::OnPrimaryFireReleased()
{
	if (Role == ROLE_Authority)
	{
		SetCannonFiring(false);
	}
	else
	{
		ServerSetCannonFiring(false);
	}
}

bool AUTVehicle_Goliath::ServerSetCannonFiring_Validate(bool)
{
	return true;
}

void AUTVehicle_Goliath::ServerSetCannonFiring_Implementation(bool bNewFiring)
{
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
	const FRotator SpawnRotation = GetCannonAimRotation();

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
	}

	NextCannonFireTime = Now + CannonFireInterval;
	GetWorldTimerManager().SetTimer(CannonFireTimerHandle, this,
		&AUTVehicle_Goliath::FireCannon, CannonFireInterval, false);
}

FRotator AUTVehicle_Goliath::GetCannonAimRotation() const
{
	FRotator AimRotation = Controller != nullptr ? Controller->GetControlRotation() : GetActorRotation();
	AimRotation.Pitch = FMath::Clamp(FRotator::NormalizeAxis(AimRotation.Pitch), -10.0f, 35.0f);
	AimRotation.Roll = 0.0f;
	return AimRotation;
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
