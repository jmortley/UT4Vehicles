#include "UTVehicle_Scorpion.h"
#include "UnrealTournament.h"
#include "WheeledVehicleMovementComponent4W.h"
#include "UTVehicleComponent.h"
#include "UTVehicleDamageType.h"
#include "UTVehicleMeshComponent.h"
#include "UTProj_ScorpionGlob.h"
#include "UTGameplayStatics.h"
#include "UTImpactEffect.h"
#include "UTProjectile.h"
#include "UnrealNetwork.h"
#include "PhysicsEngine/BodySetup.h"
#include "TireConfig.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"

namespace
{
	void ConfigureScorpionDriveFromWorkingBP(UWheeledVehicleMovementComponent4W* Movement4W, USkeletalMeshComponent* Mesh)
	{
		if (UUTVehicleMeshComponent* VehicleMesh = Cast<UUTVehicleMeshComponent>(Mesh))
		{
			// The imported tire is about 65 uu from hub to tread while the PhysX
			// wheel shape is 45 uu. Match the visible hub to the physical contact
			// patch without changing suspension, gearing, or traction.
			VehicleMesh->VisualWheelCenterOffsetZ = 20.0f;
			VehicleMesh->WeaponYawBoneName = FName(TEXT("gun_rotate"));
			VehicleMesh->WeaponPitchBoneName = FName(TEXT("gun_rotate"));
		}

		// The supplied folder has no runnable Scorpion BP. Copy the powertrain
		// defaults from working BP_GoliathTest_1, but keep wheel placement tied
		// to the Scorpion mesh's own tire bones.
		Movement4W->WheelSetups.SetNum(4);
		Movement4W->WheelSetups[0].WheelClass = UUTWheel_Scorpion_Front::StaticClass();
		Movement4W->WheelSetups[0].BoneName = FName(TEXT("F_L_Tire"));
		Movement4W->WheelSetups[1].WheelClass = UUTWheel_Scorpion_Front::StaticClass();
		Movement4W->WheelSetups[1].BoneName = FName(TEXT("F_R_Tire"));
		Movement4W->WheelSetups[2].WheelClass = UUTWheel_Scorpion_Rear::StaticClass();
		Movement4W->WheelSetups[2].BoneName = FName(TEXT("B_L_Tire"));
		Movement4W->WheelSetups[3].WheelClass = UUTWheel_Scorpion_Rear::StaticClass();
		Movement4W->WheelSetups[3].BoneName = FName(TEXT("B_R_Tire"));
		for (FWheelSetup& WheelSetup : Movement4W->WheelSetups)
		{
			WheelSetup.AdditionalOffset = FVector::ZeroVector;
		}

		// Excess lateral bite was levering the narrow chassis over before the tires
		// slipped. Downforce supplies high-speed traction without making a low-speed
		// steering input an instant rollover.
		UTireConfig* ScorpionTire = UUTWheel_Scorpion_Front::StaticClass()->GetDefaultObject<UUTWheel_Scorpion_Front>()->TireConfig;
		if (ScorpionTire != nullptr)
		{
			ScorpionTire->SetFrictionScale(1.75f);
		}

		// Working BP vehicles use mesh-specific wheel offsets. Contact alone is not
		// sufficient: the Scorpion's first corrected run contacted at -25 cm front
		// and -17 cm rear jounce, leaving the chassis collision to carry the weight
		// and the tires with no usable load. At 9 Hz, PhysX's static equilibrium is
		// gravity / frequency^2 = about +12 cm jounce. Lower the rest positions by
		// the measured delta so the suspension, rather than the chassis, supports
		// the vehicle. The first correction left the rear rest point 8 uu above the
		// front and produced a visible tail-down stance even though all four PhysX
		// wheel centers touched the floor correctly. Use one axle-center height so
		// the rear chassis comes up without changing radius or visual tire lift.
		if (Mesh != nullptr && Mesh->SkeletalMesh != nullptr)
		{
			const FVector MeshScale = Mesh->GetRelativeTransform().GetScale3D();
			for (FWheelSetup& WheelSetup : Movement4W->WheelSetups)
			{
				const FVector BonePosition = Mesh->SkeletalMesh->GetComposedRefPoseMatrix(WheelSetup.BoneName).GetOrigin() * MeshScale;
				const float TargetRestHeight = -24.5f;
				WheelSetup.AdditionalOffset.Z = TargetRestHeight - BonePosition.Z;
			}
		}

		Movement4W->EngineSetup.MaxRPM = 4000.0f;
		Movement4W->EngineSetup.MOI = 1.0f;
		Movement4W->EngineSetup.DampingRateFullThrottle = 0.05f;
		Movement4W->EngineSetup.DampingRateZeroThrottleClutchEngaged = 2.0f;
		Movement4W->EngineSetup.DampingRateZeroThrottleClutchDisengaged = 0.35f;
		FRichCurve* TorqueCurve = Movement4W->EngineSetup.TorqueCurve.GetRichCurve();
		TorqueCurve->Reset();
		// UT3's 950 uu/s cruise is slower than a UT4 dodge. Preserve the
		// Scorpion curve shape at UT4 scale: full pull through about 1900 uu/s,
		// taper at 2100, and a normal (non-boosted) top speed near 2300.
		// Preserve the heavier planted chassis, but restore the original power to
		// weight ratio and keep useful torque through the intended 2300 uu/s range.
		TorqueCurve->AddKey(0.0f, 680.0f);
		TorqueCurve->AddKey(3600.0f, 680.0f);
		TorqueCurve->AddKey(3900.0f, 150.0f);
		TorqueCurve->AddKey(4000.0f, 0.0f);

		Movement4W->TransmissionSetup.bUseGearAutoBox = true;
		Movement4W->TransmissionSetup.ClutchStrength = 10.0f;
		Movement4W->TransmissionSetup.NeutralGearUpRatio = 0.15f;
		Movement4W->TransmissionSetup.GearSwitchTime = 0.15f;
		Movement4W->TransmissionSetup.GearAutoBoxLatency = 2.0f;
		// Keep the useful wheel torque of the original 8:1 setup. Top speed is
		// supplied by the UT-style chassis assist below instead of sacrificing
		// launch and hill-climbing torque with an excessively tall final drive.
		Movement4W->TransmissionSetup.FinalRatio = 2.0f;
		Movement4W->TransmissionSetup.ReverseGearRatio = -4.0f;
		Movement4W->TransmissionSetup.ForwardGears.SetNum(1);
		Movement4W->TransmissionSetup.ForwardGears[0].Ratio = 4.0f;
		Movement4W->TransmissionSetup.ForwardGears[0].DownRatio = 0.5f;
		Movement4W->TransmissionSetup.ForwardGears[0].UpRatio = 0.5f;

		// The open diff was feeding 65% of the torque to the lightly loaded front
		// axle. One spinning front tire then drove engine RPM into the torque
		// cutoff while the chassis was still below 1000 uu/s. A tight, rear-biased
		// limited-slip setup keeps all four tire speeds useful on ramps and turns.
		Movement4W->DifferentialSetup.DifferentialType = EVehicleDifferential4W::LimitedSlip_4W;
		Movement4W->DifferentialSetup.FrontRearSplit = 0.35f;
		Movement4W->DifferentialSetup.FrontLeftRightSplit = 0.5f;
		Movement4W->DifferentialSetup.RearLeftRightSplit = 0.5f;
		Movement4W->DifferentialSetup.CentreBias = 1.3f;
		Movement4W->DifferentialSetup.FrontBias = 1.3f;
		Movement4W->DifferentialSetup.RearBias = 1.3f;

		Movement4W->Mass = 1600.0f;
		// X is the longitudinal roll axis, Y is pitch, and Z is yaw. The old
		// (1.0, 1.333, 1.333) values increased yaw resistance but left rollover
		// resistance untouched. The Scorpion is an arcade combat car, so keep yaw
		// responsive while making chassis roll much harder to start.
		Movement4W->InertiaTensorScale = FVector(6.0f, 3.0f, 1.2f);
		if (Mesh != nullptr)
		{
			// Live sprung-load telemetry showed the imported chassis placing only
			// about 9% of its static load on the front axle. The wheelbase needs the
			// COM roughly 47 uu farther forward for an even axle split. This mesh's
			// forward direction is +X (opposite the raw UT3 COMOffset convention).
			Mesh->BodyInstance.COMNudge = FVector(48.0f, 0.0f, -80.0f);
			if (Mesh->BodyInstance.IsValidBodyInstance())
			{
				// PostInitializeComponents can inherit serialized BodyInstance values.
				// Updating the field alone does not move PhysX's already-created COM.
				Mesh->BodyInstance.UpdateMassProperties();
			}
		}

		FRichCurve* SteeringCurve = Movement4W->SteeringCurve.GetRichCurve();
		SteeringCurve->Reset();
		// UE4's curve input is km/h. Keep high-speed and boost steering at exactly
		// 90 percent of the low-speed response: 0.54 versus 0.60. Ease into that
		// small reduction so steering never becomes more responsive as speed rises.
		SteeringCurve->AddKey(0.0f, 0.60f);
		SteeringCurve->AddKey(15.0f, 0.60f);
		SteeringCurve->AddKey(43.2f, 0.58f);
		SteeringCurve->AddKey(79.2f, 0.55f);
		SteeringCurve->AddKey(90.0f, 0.54f);
		SteeringCurve->AddKey(158.4f, 0.54f);
	}
}

AUTVehicle_Scorpion::AUTVehicle_Scorpion(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bBladesExtended = false;
	bLeftBladeBroken = false;
	bRightBladeBroken = false;
	BladeDamage = 1000.0f;
	BladeTraceRadius = 18.0f;
	LeftBladeStartSocket = FName(TEXT("Blade_L_Start"));
	LeftBladeEndSocket = FName(TEXT("Blade_L_End"));
	RightBladeStartSocket = FName(TEXT("Blade_R_Start"));
	RightBladeEndSocket = FName(TEXT("Blade_R_End"));

	bBoostersActivated = false;
	bSelfDestructArmed = false;
	MaxBoostDuration = 2.0f;
	BoostRechargeDuration = 5.0f;
	BoostTargetSpeed = 4400.0f;
	BoostAcceleration = 8500.0f;
	static ConstructorHelpers::FClassFinder<AUTProjectile> GrenadeProjectileFinder(
		TEXT("/Game/RestrictedAssets/Weapons/GrenadeLauncher/BP_Grenade"));
	GunProjectileClass = GrenadeProjectileFinder.Succeeded()
		? GrenadeProjectileFinder.Class
		: AUTProj_ScorpionGlob::StaticClass();
	GunMuzzleSocket = FName(TEXT("TurretFireSocket"));
	GunFireInterval = 0.65f;
	GunAimRotationRate = 360.0f;
	static ConstructorHelpers::FObjectFinder<UParticleSystem> GunMuzzleEffectFinder(
		TEXT("/Game/RestrictedAssets/Weapons/GrenadeLauncher/Effects/P_GrenadeLauncher_MF"));
	GunMuzzleEffect = GunMuzzleEffectFinder.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> GunFireSoundFinder(
		TEXT("/Game/Mogno/Vehicles/ScorpionTest/SFX/A_Vehicle_Scorpion_AltFire01"));
	GunFireSound = GunFireSoundFinder.Object;
	BoostDamageMultiplier = 1.5f;
	GroundDownforceAcceleration = 3200.0f;
	GroundDownforceTargetSpeed = 1800.0f;
	GroundStabilityTraceDistance = 250.0f;
	GroundUprightAcceleration = 35.0f;
	GroundRollDamping = 12.0f;
	GroundPitchDamping = 7.0f;
	VacantStopDeceleration = 6000.0f;
	// A UT4 dodge peaks around 1700 uu/s. Normal Scorpion travel must separate
	// decisively from infantry, while boost remains a distinct upper band.
	NormalDriveTargetSpeed = 2800.0f;
	NormalDriveAcceleration = 4500.0f;
	SelfDestructMinBoostTime = 0.15f;
	SelfDestructMinSpeed = 1800.0f;
	SelfDestructFuseDuration = 2.5f;
	SelfDestructDamage = 600.0f;
	SelfDestructRadius = 600.0f;
	SelfDestructMomentum = 200000.0f;
	SelfDestructEffectScale = 2.5f;
	SelfDestructBlastScale = 2.25f;
	SelfDestructCanopyBone = FName(TEXT("Hatch_Slide"));
	SelfDestructCanopyLaunchVelocity = FVector(250.0f, 0.0f, 900.0f);
	SelfDestructCanopyAngularVelocity = FVector(180.0f, 420.0f, 260.0f);
	SelfDestructCanopyLifetime = 6.0f;
	ArmedDamageMultiplier = 2.0f;
	BoostStartTime = -1000.0f;
	NextBoostTime = -1000.0f;
	NextGunFireTime = -1000.0f;
	LastGunAimSendTime = -1000.0f;
	NormalDriveThrottle = 0.0f;
	LastSentNormalDriveThrottle = 0.0f;
	bHavePreviousBladePositions = false;
	bHasSelfDestructed = false;
	bGunFiring = false;
	ReplicatedGunAim = FRotator::ZeroRotator;
	CurrentGunAim = FRotator::ZeroRotator;
	LastSentGunAim = FRotator::ZeroRotator;
	SelfDestructInstigator = nullptr;

	// Mesh setup
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshFinder(
		TEXT("/Game/RestrictedAssets/Proto/UT3_Vehicles/VH_Scorpion/Meshes/SK_VH_Scorpion_001"));
	if (MeshFinder.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(MeshFinder.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(
		TEXT("/Game/RestrictedAssets/Proto/UT3_Vehicles/VH_Scorpion/Materials/M_Scorpion"));
	if (MatFinder.Succeeded())
	{
		GetMesh()->SetMaterial(0, MatFinder.Object);
	}
	static ConstructorHelpers::FObjectFinder<UParticleSystem> SelfDestructEffectFinder(
		TEXT("/Game/RestrictedAssets/Proto/UT3_Vehicles/VH_Scorpion/Effects/P_VH_Scorpion_SelfDestruct"));
	SelfDestructEffect = SelfDestructEffectFinder.Object;
	static ConstructorHelpers::FClassFinder<AUTImpactEffect> SelfDestructBlastFinder(
		TEXT("/Game/RestrictedAssets/Weapons/GrenadeLauncher/GrenadeExplodeBig"));
	SelfDestructBlastEffect = SelfDestructBlastFinder.Class;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SelfDestructCanopyFinder(
		TEXT("/Game/RestrictedAssets/Proto/UT3_Vehicles/VH_Scorpion/Meshes/S_FX_ScorpionCanopy"));
	SelfDestructCanopyMesh = SelfDestructCanopyFinder.Object;

	// Vehicle component setup
	if (VehicleComponent != nullptr)
	{
		VehicleComponent->Health = 600;
		VehicleComponent->HealthMax = 600;
		VehicleComponent->bLightArmor = true;
		VehicleComponent->EntryRadius = 350.0f;
	}

	// Full-vehicle chase camera: the old 550 uu arm cropped the rear wheels and
	// read more like a character camera. The longer arm keeps the entire
	// Scorpion visible with some road around it, like a Forza-style chase view.
	if (SpringArm != nullptr)
	{
		SpringArm->TargetArmLength = 1150.0f;
		SpringArm->SetRelativeLocation(FVector(75.0f, 0.0f, 475.0f));
		SpringArm->SetRelativeRotation(FRotator(-7.0f, 0.0f, 0.0f));
		SpringArm->TargetOffset = FVector::ZeroVector;
		SpringArm->bUsePawnControlRotation = true;
		SpringArm->bInheritPitch = true;
		SpringArm->bInheritYaw = true;
		SpringArm->bInheritRoll = false;
		SpringArm->bEnableCameraLag = true;
		SpringArm->CameraLagSpeed = 8.0f;
	}
	if (Camera != nullptr)
	{
		Camera->SetFieldOfView(90.0f);
	}

	// Use the original UT3 Scorpion recordings imported into the UT4 project.
	// The engine wave is played directly, so explicitly loop it rather than
	// relying on the AudioComponent to restart a finished one-shot.
	static ConstructorHelpers::FObjectFinder<USoundBase> EngineLoopFinder(
		TEXT("/Game/Mogno/Vehicles/ScorpionTest/SFX/A_Vehicle_Scorpion_EngineLoop01"));
	static ConstructorHelpers::FObjectFinder<USoundBase> EngineStartFinder(
		TEXT("/Game/Mogno/Vehicles/ScorpionTest/SFX/A_Vehicle_Scorpion_Start01"));
	static ConstructorHelpers::FObjectFinder<USoundBase> EngineStopFinder(
		TEXT("/Game/Mogno/Vehicles/ScorpionTest/SFX/A_Vehicle_Scorpion_Stop01"));
	static ConstructorHelpers::FObjectFinder<USoundBase> ImpactFinder(
		TEXT("/Game/Mogno/Vehicles/ScorpionTest/SFX/A_Vehicle_Scorpion_Collide_Cue"));
	EngineLoopSound = EngineLoopFinder.Object;
	EngineStartSound = EngineStartFinder.Object;
	EngineStopSound = EngineStopFinder.Object;
	ImpactSound = ImpactFinder.Object;
	if (USoundWave* EngineLoopWave = Cast<USoundWave>(EngineLoopSound))
	{
		EngineLoopWave->bLooping = true;
	}

	// 4W Movement Component setup
	UWheeledVehicleMovementComponent4W* Movement4W = CastChecked<UWheeledVehicleMovementComponent4W>(GetVehicleMovementComponent());
	ConfigureScorpionDriveFromWorkingBP(Movement4W, GetMesh());

	// Note: ThrottleInputRate, BrakeInputRate, SteeringInputRate are protected
	// in UWheeledVehicleMovementComponent. Tune via Blueprint defaults if needed.
}

void AUTVehicle_Scorpion::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	RefreshBladeVisuals();
	if (GetMesh() != nullptr)
	{
		GetMesh()->OnComponentHit.AddUniqueDynamic(this, &AUTVehicle_Scorpion::OnArmedVehicleHit);
	}

	UWheeledVehicleMovementComponent4W* Movement4W = Cast<UWheeledVehicleMovementComponent4W>(GetVehicleMovementComponent());
	if (Movement4W == nullptr)
	{
		return;
	}

	// The existing map actor serialized the earlier native component values,
	// so constructor changes alone do not replace them. Reapply the verified
	// BP setup before rebuilding PhysX on every instance.
	ConfigureScorpionDriveFromWorkingBP(Movement4W, GetMesh());
	if (SpringArm != nullptr)
	{
		SpringArm->TargetArmLength = 1150.0f;
		SpringArm->SetRelativeLocation(FVector(75.0f, 0.0f, 475.0f));
		SpringArm->SetRelativeRotation(FRotator(-7.0f, 0.0f, 0.0f));
		SpringArm->TargetOffset = FVector::ZeroVector;
		SpringArm->bUsePawnControlRotation = true;
		SpringArm->bInheritPitch = true;
		SpringArm->bInheritYaw = true;
		SpringArm->bInheritRoll = false;
		SpringArm->bEnableCameraLag = true;
		SpringArm->CameraLagSpeed = 8.0f;
	}
	if (Camera != nullptr)
	{
		Camera->SetFieldOfView(90.0f);
	}

	if (Movement4W->IsRegistered())
	{
		Movement4W->RecreatePhysicsState();
		FBodyInstance* ScorpionChassisBody = GetMesh() != nullptr ? GetMesh()->GetBodyInstance() : nullptr;
		const FString ChassisBoneName = ScorpionChassisBody != nullptr && ScorpionChassisBody->BodySetup != nullptr
			? ScorpionChassisBody->BodySetup->BoneName.ToString()
			: TEXT("NONE");
		const FBox ChassisBounds = ScorpionChassisBody != nullptr && ScorpionChassisBody->BodySetup != nullptr
			? ScorpionChassisBody->BodySetup->AggGeom.CalcAABB(FTransform(FQuat::Identity, FVector::ZeroVector, GetMesh()->GetRelativeTransform().GetScale3D()))
			: FBox(ForceInit);
		UTireConfig* ScorpionTire = UUTWheel_Scorpion_Front::StaticClass()->GetDefaultObject<UUTWheel_Scorpion_Front>()->TireConfig;
		float ScorpionPeakTorque = 0.0f;
		for (const FRichCurveKey& TorqueKey : Movement4W->EngineSetup.TorqueCurve.GetRichCurveConst()->GetCopyOfKeys())
		{
			ScorpionPeakTorque = FMath::Max(ScorpionPeakTorque, TorqueKey.Value);
		}
		const FVector LocalCOM = GetMesh()->GetComponentTransform().InverseTransformPosition(
			GetMesh()->GetCenterOfMass());
		UE_LOG(LogTemp, Warning, TEXT("[VehiclePhysics] Applied Scorpion drive setup Physics=%d ChassisBone=%s ChassisZ=(%.2f..%.2f) COM=%s COMNudge=%s WheelOffsetZ=(%.2f,%.2f,%.2f,%.2f) MeshClass=%s Tire=%s Friction=%.2f FinalRatio=%.2f PeakTorque=%.1f Diff=%d FrontSplit=%.2f Cruise=%.0f Assist=%.0f Boost=%.0f"),
			Movement4W->HasValidPhysicsState() ? 1 : 0,
			*ChassisBoneName,
			ChassisBounds.IsValid ? ChassisBounds.Min.Z : 0.0f,
			ChassisBounds.IsValid ? ChassisBounds.Max.Z : 0.0f,
			*LocalCOM.ToString(),
			*GetMesh()->BodyInstance.COMNudge.ToString(),
			Movement4W->WheelSetups[0].AdditionalOffset.Z,
			Movement4W->WheelSetups[1].AdditionalOffset.Z,
			Movement4W->WheelSetups[2].AdditionalOffset.Z,
			Movement4W->WheelSetups[3].AdditionalOffset.Z,
			*GetNameSafe(GetMesh()->GetClass()),
			*GetNameSafe(ScorpionTire),
			ScorpionTire != nullptr ? ScorpionTire->GetFrictionScale() : 1.0f,
			Movement4W->TransmissionSetup.FinalRatio,
			ScorpionPeakTorque,
			(int32)Movement4W->DifferentialSetup.DifferentialType,
			Movement4W->DifferentialSetup.FrontRearSplit,
			NormalDriveTargetSpeed,
			NormalDriveAcceleration,
			BoostTargetSpeed);
	}
}

void AUTVehicle_Scorpion::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateGunAim(DeltaSeconds);

	// Wheeled physics runs on the server and on the locally controlled proxy.
	// Simulated proxies receive the resulting replicated transform and must not
	// apply a second, independent stabilization force.
	if (Role == ROLE_Authority || IsLocallyControlled())
	{
		ApplyGroundStability(DeltaSeconds);
	}
	ApplyVacantStop(DeltaSeconds);

	if (Role == ROLE_Authority)
	{
		if (bBladesExtended && !bSelfDestructArmed)
		{
			UpdateBladeTraces();
		}
		else
		{
			bHavePreviousBladePositions = false;
		}

		if (bBoostersActivated || bSelfDestructArmed)
		{
			ApplyBoostForce();
		}
	}
	if ((Role == ROLE_Authority || IsLocallyControlled()) &&
		!bBoostersActivated && !bSelfDestructArmed)
	{
		// Dedicated-server driving input lives on the autonomous proxy, which is
		// also where the PhysX wheeled input is applied. Run normal assist there;
		// the authority copy has no raw ThrottleAxisValue for a remote driver.
		ApplyNormalDriveAssist();
	}
}

void AUTVehicle_Scorpion::ApplyVacantStop(float DeltaSeconds)
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (Mesh == nullptr || !Mesh->IsSimulatingPhysics() || VehicleComponent == nullptr ||
		VehicleComponent->HasDriver() || VehicleComponent->bDead || bSelfDestructArmed)
	{
		return;
	}

	// Apply on every PhysX copy. The service brake still handles tire rotation;
	// this removes the long neutral-like coast that remains after possession is
	// cleared, without affecting the deliberately driverless armed eject car.
	FVector Velocity = Mesh->GetPhysicsLinearVelocity();
	FVector PlanarVelocity(Velocity.X, Velocity.Y, 0.0f);
	PlanarVelocity = FMath::VInterpConstantTo(PlanarVelocity, FVector::ZeroVector,
		DeltaSeconds, FMath::Max(VacantStopDeceleration, 0.0f));
	Velocity.X = PlanarVelocity.X;
	Velocity.Y = PlanarVelocity.Y;
	Mesh->SetAllPhysicsLinearVelocity(Velocity);

	FVector AngularVelocity = Mesh->GetPhysicsAngularVelocity();
	AngularVelocity = FMath::VInterpConstantTo(AngularVelocity, FVector::ZeroVector,
		DeltaSeconds, 540.0f);
	Mesh->SetAllPhysicsAngularVelocity(AngularVelocity);
	if (PlanarVelocity.SizeSquared() < FMath::Square(10.0f) && FMath::Abs(Velocity.Z) < 50.0f)
	{
		Velocity.X = 0.0f;
		Velocity.Y = 0.0f;
		Mesh->SetAllPhysicsLinearVelocity(Velocity);
		Mesh->SetAllPhysicsAngularVelocity(FVector::ZeroVector);
	}
}

void AUTVehicle_Scorpion::ApplyGroundStability(float DeltaSeconds)
{
	USkeletalMeshComponent* Mesh = GetMesh();
	UWheeledVehicleMovementComponent4W* Movement4W =
		Cast<UWheeledVehicleMovementComponent4W>(GetVehicleMovementComponent());
	if (Mesh == nullptr || Movement4W == nullptr || !Mesh->IsSimulatingPhysics() ||
		VehicleComponent == nullptr || VehicleComponent->bDead || GetWorld() == nullptr)
	{
		return;
	}

	const FVector ChassisUp = Mesh->GetUpVector();
	// Do not pin an already overturned vehicle onto its side. The stabilizer is
	// preventative and releases naturally once a real jump leaves the ground.
	if (FVector::DotProduct(ChassisUp, FVector::UpVector) < 0.35f)
	{
		return;
	}

	const FVector TraceStart = Mesh->GetComponentLocation() + FVector::UpVector * 25.0f;
	const FVector TraceEnd = TraceStart - FVector::UpVector * GroundStabilityTraceDistance;
	FCollisionQueryParams QueryParams(FName(TEXT("ScorpionGroundStability")), false, this);
	FHitResult GroundHit;
	if (!GetWorld()->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd,
		ECC_WorldStatic, QueryParams))
	{
		return;
	}

	const FVector GroundNormal = GroundHit.ImpactNormal.GetSafeNormal();
	const float PlanarSpeed = FVector::VectorPlaneProject(Mesh->GetPhysicsLinearVelocity(), GroundNormal).Size();
	const float SpeedAlpha = FMath::Clamp(PlanarSpeed / FMath::Max(GroundDownforceTargetSpeed, 1.0f), 0.0f, 1.0f);
	// Squared speed gives the familiar aerodynamic downforce curve. bAccelChange
	// keeps the handling tune independent of any later mass adjustment.
	Mesh->AddForce(-GroundNormal * GroundDownforceAcceleration * SpeedAlpha * SpeedAlpha,
		NAME_None, true);

	const FVector UprightAxis = FVector::CrossProduct(ChassisUp, GroundNormal);
	Mesh->AddTorque(UprightAxis * GroundUprightAcceleration, NAME_None, true);

	// Retain yaw authority, but bleed roll/pitch angular velocity before load can
	// transfer far enough to lift an entire side of the car.
	const FTransform MeshTransform = Mesh->GetComponentTransform();
	FVector LocalAngularVelocity = MeshTransform.InverseTransformVectorNoScale(
		Mesh->GetPhysicsAngularVelocity());
	LocalAngularVelocity.X *= FMath::Exp(-GroundRollDamping * DeltaSeconds);
	LocalAngularVelocity.Y *= FMath::Exp(-GroundPitchDamping * DeltaSeconds);
	Mesh->SetPhysicsAngularVelocity(
		MeshTransform.TransformVectorNoScale(LocalAngularVelocity), false);
}

void AUTVehicle_Scorpion::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld() != nullptr)
	{
		GetWorldTimerManager().ClearTimer(BoostTimerHandle);
		GetWorldTimerManager().ClearTimer(SelfDestructTimerHandle);
		GetWorldTimerManager().ClearTimer(GunFireTimerHandle);
	}
	if (GetMesh() != nullptr)
	{
		GetMesh()->OnComponentHit.RemoveDynamic(this, &AUTVehicle_Scorpion::OnArmedVehicleHit);
	}

	Super::EndPlay(EndPlayReason);
}

void AUTVehicle_Scorpion::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AUTVehicle_Scorpion, bBladesExtended);
	DOREPLIFETIME(AUTVehicle_Scorpion, bLeftBladeBroken);
	DOREPLIFETIME(AUTVehicle_Scorpion, bRightBladeBroken);
	DOREPLIFETIME(AUTVehicle_Scorpion, bBoostersActivated);
	DOREPLIFETIME(AUTVehicle_Scorpion, bSelfDestructArmed);
	DOREPLIFETIME_CONDITION(AUTVehicle_Scorpion, ReplicatedGunAim, COND_SkipOwner);
}

FRotator AUTVehicle_Scorpion::SanitizeGunAim(const FRotator& AimRotation) const
{
	FRotator Result;
	Result.Pitch = FMath::Clamp(FRotator::NormalizeAxis(AimRotation.Pitch), -15.0f, 65.0f);
	Result.Yaw = FRotator::NormalizeAxis(AimRotation.Yaw);
	Result.Roll = 0.0f;
	return Result;
}

FRotator AUTVehicle_Scorpion::GetDesiredGunAimRotation() const
{
	const FRotator WorldAim = Controller != nullptr
		? Controller->GetControlRotation()
		: GetActorRotation();
	const USkeletalMeshComponent* Mesh = GetMesh();
	if (Mesh == nullptr)
	{
		return SanitizeGunAim(WorldAim - GetActorRotation());
	}
	const FVector LocalAimDirection = Mesh->GetComponentTransform().InverseTransformVectorNoScale(
		WorldAim.Vector());
	return SanitizeGunAim(LocalAimDirection.Rotation());
}

void AUTVehicle_Scorpion::UpdateGunAim(float DeltaSeconds)
{
	const bool bOccupied = VehicleComponent != nullptr && VehicleComponent->HasDriver() &&
		!VehicleComponent->bDead;
	if (bOccupied && IsLocallyControlled())
	{
		const FRotator DesiredAim = GetDesiredGunAimRotation();
		ReplicatedGunAim = DesiredAim;
		if (Role < ROLE_Authority && GetWorld() != nullptr)
		{
			const float Now = GetWorld()->GetTimeSeconds();
			const bool bAimChanged =
				FMath::Abs(FMath::FindDeltaAngleDegrees(LastSentGunAim.Yaw, DesiredAim.Yaw)) >= 0.25f ||
				FMath::Abs(FMath::FindDeltaAngleDegrees(LastSentGunAim.Pitch, DesiredAim.Pitch)) >= 0.25f;
			if (bAimChanged && Now - LastGunAimSendTime >= 0.05f)
			{
				LastSentGunAim = DesiredAim;
				LastGunAimSendTime = Now;
				ServerSetGunAim(DesiredAim);
			}
		}
	}

	CurrentGunAim = FMath::RInterpConstantTo(CurrentGunAim,
		SanitizeGunAim(ReplicatedGunAim), DeltaSeconds, GunAimRotationRate);
	if (UUTVehicleMeshComponent* VehicleMesh = Cast<UUTVehicleMeshComponent>(GetMesh()))
	{
		VehicleMesh->SetWeaponAimRotation(CurrentGunAim);
	}
}

bool AUTVehicle_Scorpion::ServerSetGunAim_Validate(FRotator NewAimRotation)
{
	return FMath::IsFinite(NewAimRotation.Pitch) && FMath::IsFinite(NewAimRotation.Yaw) &&
		FMath::IsFinite(NewAimRotation.Roll);
}

void AUTVehicle_Scorpion::ServerSetGunAim_Implementation(FRotator NewAimRotation)
{
	if (VehicleComponent != nullptr && VehicleComponent->HasDriver() && !VehicleComponent->bDead)
	{
		ReplicatedGunAim = SanitizeGunAim(NewAimRotation);
		ForceNetUpdate();
	}
}

void AUTVehicle_Scorpion::OnRep_GunAim()
{
	ReplicatedGunAim = SanitizeGunAim(ReplicatedGunAim);
}

void AUTVehicle_Scorpion::OnThrottleInput(float Value)
{
	Super::OnThrottleInput(Value);

	const float NewThrottle = FMath::Clamp(Value, 0.0f, 1.0f);
	NormalDriveThrottle = NewThrottle;
	if (Role < ROLE_Authority &&
		(FMath::Abs(NewThrottle - LastSentNormalDriveThrottle) >= 0.1f ||
		 (NewThrottle <= 0.01f) != (LastSentNormalDriveThrottle <= 0.01f)))
	{
		LastSentNormalDriveThrottle = NewThrottle;
		ServerSetNormalDriveThrottle(NewThrottle);
	}
}

void AUTVehicle_Scorpion::OnHandbrakePressed()
{
	// UT3 puts boost on the jump/handbrake input while accelerating. Preserve
	// the ordinary handbrake when the driver is stopped or reversing.
	if (ThrottleAxisValue > 0.1f)
	{
		RequestBoost();
	}
	else
	{
		Super::OnHandbrakePressed();
	}
}

void AUTVehicle_Scorpion::OnHandbrakeReleased()
{
	Super::OnHandbrakeReleased();
}

void AUTVehicle_Scorpion::OnPrimaryFirePressed()
{
	if (Role == ROLE_Authority)
	{
		ReplicatedGunAim = GetDesiredGunAimRotation();
		SetGunFiring(true);
	}
	else
	{
		ServerSetGunFiring(true, GetDesiredGunAimRotation());
	}
}

void AUTVehicle_Scorpion::OnPrimaryFireReleased()
{
	if (Role == ROLE_Authority)
	{
		ReplicatedGunAim = GetDesiredGunAimRotation();
		SetGunFiring(false);
	}
	else
	{
		ServerSetGunFiring(false, GetDesiredGunAimRotation());
	}
}

bool AUTVehicle_Scorpion::ServerSetGunFiring_Validate(bool, FRotator NewAimRotation)
{
	return FMath::IsFinite(NewAimRotation.Pitch) && FMath::IsFinite(NewAimRotation.Yaw) &&
		FMath::IsFinite(NewAimRotation.Roll);
}

void AUTVehicle_Scorpion::ServerSetGunFiring_Implementation(bool bNewFiring,
	FRotator NewAimRotation)
{
	ReplicatedGunAim = SanitizeGunAim(NewAimRotation);
	SetGunFiring(bNewFiring);
}

void AUTVehicle_Scorpion::SetGunFiring(bool bNewFiring)
{
	if (Role != ROLE_Authority)
	{
		return;
	}
	bGunFiring = bNewFiring && VehicleComponent != nullptr &&
		VehicleComponent->HasDriver() && !VehicleComponent->bDead;
	if (GetWorld() != nullptr)
	{
		GetWorldTimerManager().ClearTimer(GunFireTimerHandle);
	}
	if (bGunFiring)
	{
		FireGun();
	}
}

FRotator AUTVehicle_Scorpion::GetGunAimRotation() const
{
	const USkeletalMeshComponent* Mesh = GetMesh();
	const FRotator ProjectileAim = SanitizeGunAim(ReplicatedGunAim);
	const FVector WorldAimDirection = Mesh != nullptr
		? Mesh->GetComponentTransform().TransformVectorNoScale(ProjectileAim.Vector())
		: GetActorTransform().TransformVectorNoScale(ProjectileAim.Vector());
	return WorldAimDirection.Rotation();
}

FRotator AUTVehicle_Scorpion::GetAdjustedProjectileAimRotation() const
{
	FRotator AdjustedAim = GetGunAimRotation();
	const float LocalPitch = SanitizeGunAim(ReplicatedGunAim).Pitch;
	// UT3 nudges player-fired globs upward to compensate for their falling arc.
	AdjustedAim.Pitch += LocalPitch >= 0.0f ? (90.0f - LocalPitch) / 16.0f : 5.625f;
	AdjustedAim.Roll = 0.0f;
	return AdjustedAim;
}

void AUTVehicle_Scorpion::FireGun()
{
	if (Role != ROLE_Authority || GetWorld() == nullptr || !bGunFiring ||
		VehicleComponent == nullptr || !VehicleComponent->HasDriver() || VehicleComponent->bDead)
	{
		SetGunFiring(false);
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextGunFireTime)
	{
		GetWorldTimerManager().SetTimer(GunFireTimerHandle, this,
			&AUTVehicle_Scorpion::FireGun, NextGunFireTime - Now, false);
		return;
	}

	USkeletalMeshComponent* Mesh = GetMesh();
	FVector SpawnLocation = GetActorLocation() + GetActorForwardVector() * 220.0f +
		FVector(0.0f, 0.0f, 120.0f);
	if (Mesh != nullptr && Mesh->DoesSocketExist(GunMuzzleSocket))
	{
		SpawnLocation = Mesh->GetSocketLocation(GunMuzzleSocket);
	}
	const FRotator SpawnRotation = GetAdjustedProjectileAimRotation();

	// BP_Grenade reads its owner/instigator character to select team visuals.
	// The hidden driver remains the authoritative character while possessing the
	// Scorpion, so preserve that contract instead of giving the Blueprint a
	// vehicle owner that its BeginPlay graph cannot cast.
	APawn* ProjectileInstigator = VehicleComponent->Driver != nullptr
		? VehicleComponent->Driver
		: static_cast<APawn*>(this);
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = ProjectileInstigator;
	SpawnParameters.Instigator = ProjectileInstigator;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UClass* ProjectileClass = GunProjectileClass.Get() != nullptr
		? GunProjectileClass.Get()
		: AUTProj_ScorpionGlob::StaticClass();
	AUTProjectile* Grenade = GetWorld()->SpawnActor<AUTProjectile>(
		ProjectileClass, SpawnLocation, SpawnRotation, SpawnParameters);
	if (Grenade != nullptr)
	{
		Grenade->InstigatorController = Controller != nullptr
			? Controller
			: VehicleComponent->DamageInstigator;
		Grenade->InstigatorTeamNum = GetTeamNum();
		if (Grenade->CollisionComp != nullptr)
		{
			// The muzzle is outside the chassis, but explicitly ignoring the firing
			// vehicle prevents a bounce from clipping the fast-moving Scorpion.
			Grenade->CollisionComp->IgnoreActorWhenMoving(this, true);
		}
		MulticastPlayGunFire(SpawnLocation, SpawnRotation);
	}

	NextGunFireTime = Now + GunFireInterval;
	GetWorldTimerManager().SetTimer(GunFireTimerHandle, this,
		&AUTVehicle_Scorpion::FireGun, GunFireInterval, false);
}

void AUTVehicle_Scorpion::MulticastPlayGunFire_Implementation(
	FVector MuzzleLocation, FRotator MuzzleRotation)
{
	if (GetWorld() == nullptr || GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (GunMuzzleEffect != nullptr)
	{
		UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), GunMuzzleEffect,
			MuzzleLocation, MuzzleRotation, true);
	}
	if (GunFireSound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(this, GunFireSound, MuzzleLocation,
			1.0f, 1.0f, 0.0f, VehicleSoundAttenuation);
	}
}

void AUTVehicle_Scorpion::OnAltFirePressed()
{
	if (Role == ROLE_Authority)
	{
		SetBladesExtended(true);
	}
	else
	{
		ServerSetBladesExtended(true);
	}
}

void AUTVehicle_Scorpion::OnAltFireReleased()
{
	if (Role == ROLE_Authority)
	{
		SetBladesExtended(false);
	}
	else
	{
		ServerSetBladesExtended(false);
	}
}

bool AUTVehicle_Scorpion::HandleDriverLeaveRequest()
{
	if (Role == ROLE_Authority)
	{
		// Never carry the previous driver's held-throttle state into the next seat.
		NormalDriveThrottle = 0.0f;
		SetGunFiring(false);
	}
	const bool bReadyToEject = Role == ROLE_Authority && ReadyToSelfDestruct();
	if (Role == ROLE_Authority)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScorpionEject] Request Vehicle=%s Active=%d Armed=%d Elapsed=%.3f Speed=%.1f Ready=%d"),
			*GetName(), bBoostersActivated ? 1 : 0, bSelfDestructArmed ? 1 : 0,
			GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() - BoostStartTime : 0.0f,
			GetVelocity().Size(), bReadyToEject ? 1 : 0);
	}
	if (bReadyToEject)
	{
		return ArmSelfDestructAndEject();
	}

	if (Role == ROLE_Authority)
	{
		StopBoost();
		SetBladesExtended(false);
	}
	return Super::HandleDriverLeaveRequest();
}

bool AUTVehicle_Scorpion::ShouldApplyVacantBrake() const
{
	// Armed boost-eject is intentionally a driverless projectile. Every other
	// Scorpion exit gets the shared automatic vacant-vehicle brake.
	return !bSelfDestructArmed && Super::ShouldApplyVacantBrake();
}

bool AUTVehicle_Scorpion::ServerSetBladesExtended_Validate(bool)
{
	return true;
}

void AUTVehicle_Scorpion::ServerSetBladesExtended_Implementation(bool bExtended)
{
	if (VehicleComponent != nullptr && VehicleComponent->HasDriver() && !VehicleComponent->bDead)
	{
		SetBladesExtended(bExtended);
	}
}

void AUTVehicle_Scorpion::SetBladesExtended(bool bExtended)
{
	if (Role != ROLE_Authority)
	{
		return;
	}

	const bool bNewState = bExtended && !bSelfDestructArmed &&
		(!bLeftBladeBroken || !bRightBladeBroken);
	if (bBladesExtended != bNewState)
	{
		bBladesExtended = bNewState;
		bHavePreviousBladePositions = false;
		OnRep_BladeState();
		ForceNetUpdate();
	}
}

void AUTVehicle_Scorpion::OnRep_BladeState()
{
	RefreshBladeVisuals();
	if (!bBladesExtended)
	{
		bHavePreviousBladePositions = false;
	}
}

void AUTVehicle_Scorpion::RefreshBladeVisuals()
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (Mesh == nullptr)
	{
		return;
	}

	const bool bShowLeft = bBladesExtended && !bLeftBladeBroken;
	const bool bShowRight = bBladesExtended && !bRightBladeBroken;
	if (bShowLeft)
	{
		Mesh->UnHideBoneByName(FName(TEXT("Blade_L1")));
		Mesh->UnHideBoneByName(FName(TEXT("Blade_L2")));
	}
	else
	{
		Mesh->HideBoneByName(FName(TEXT("Blade_L1")), PBO_None);
		Mesh->HideBoneByName(FName(TEXT("Blade_L2")), PBO_None);
	}
	if (bShowRight)
	{
		Mesh->UnHideBoneByName(FName(TEXT("Blade_R1")));
		Mesh->UnHideBoneByName(FName(TEXT("Blade_R2")));
	}
	else
	{
		Mesh->HideBoneByName(FName(TEXT("Blade_R1")), PBO_None);
		Mesh->HideBoneByName(FName(TEXT("Blade_R2")), PBO_None);
	}
}

void AUTVehicle_Scorpion::UpdateBladeTraces()
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (Mesh == nullptr || GetWorld() == nullptr)
	{
		return;
	}

	const FVector LeftStart = Mesh->GetSocketLocation(LeftBladeStartSocket);
	const FVector LeftEnd = Mesh->GetSocketLocation(LeftBladeEndSocket);
	const FVector RightStart = Mesh->GetSocketLocation(RightBladeStartSocket);
	const FVector RightEnd = Mesh->GetSocketLocation(RightBladeEndSocket);

	if (bHavePreviousBladePositions)
	{
		if (!bLeftBladeBroken)
		{
			TraceBlade(true, PreviousLeftBladeStart, PreviousLeftBladeEnd, LeftStart, LeftEnd);
		}
		if (!bRightBladeBroken)
		{
			TraceBlade(false, PreviousRightBladeStart, PreviousRightBladeEnd, RightStart, RightEnd);
		}
	}

	PreviousLeftBladeStart = LeftStart;
	PreviousLeftBladeEnd = LeftEnd;
	PreviousRightBladeStart = RightStart;
	PreviousRightBladeEnd = RightEnd;
	bHavePreviousBladePositions = true;
}

void AUTVehicle_Scorpion::TraceBlade(bool bLeftBlade, const FVector& PreviousStart,
	const FVector& PreviousEnd, const FVector& CurrentStart, const FVector& CurrentEnd)
{
	FCollisionQueryParams QueryParams(FName(TEXT("ScorpionBlade")), false, this);
	if (VehicleComponent != nullptr && VehicleComponent->Driver != nullptr)
	{
		QueryParams.AddIgnoredActor(VehicleComponent->Driver);
	}
	const FCollisionShape BladeShape = FCollisionShape::MakeSphere(BladeTraceRadius);

	// The blade moves as a swept segment. Check its current span first, then the
	// socket paths and center path so fast turns cannot tunnel a blade through a
	// character between server frames.
	const FVector PreviousMiddle = (PreviousStart + PreviousEnd) * 0.5f;
	const FVector CurrentMiddle = (CurrentStart + CurrentEnd) * 0.5f;
	const FVector TraceStarts[] = { CurrentStart, PreviousStart, PreviousEnd, PreviousMiddle };
	const FVector TraceEnds[] = { CurrentEnd, CurrentStart, CurrentEnd, CurrentMiddle };
	for (int32 TraceIndex = 0; TraceIndex < 4; ++TraceIndex)
	{
		FHitResult Hit;
		if (GetWorld()->SweepSingleByChannel(Hit, TraceStarts[TraceIndex], TraceEnds[TraceIndex],
			FQuat::Identity, COLLISION_TRACE_WEAPON, BladeShape, QueryParams))
		{
			HandleBladeHit(bLeftBlade, Hit);
			return;
		}
	}
}

void AUTVehicle_Scorpion::HandleBladeHit(bool bLeftBlade, const FHitResult& Hit)
{
	AActor* HitActor = Hit.GetActor();
	if (HitActor == nullptr || HitActor == this)
	{
		BreakBlade(bLeftBlade);
		return;
	}

	// UT3 blades are lethal to infantry but snap on vehicles and scenery.
	// Vehicles are components rather than one concrete pawn hierarchy here.
	if (HitActor->FindComponentByClass<UUTVehicleComponent>() != nullptr)
	{
		BreakBlade(bLeftBlade);
		return;
	}

	APawn* HitPawn = Cast<APawn>(HitActor);
	if (HitPawn == nullptr)
	{
		BreakBlade(bLeftBlade);
		return;
	}

	const FVector BladeDirection = GetVelocity().GetSafeNormal();
	const FVector Momentum = GetVelocity() * 100.0f;
	HitPawn->TakeDamage(BladeDamage,
		FUTPointDamageEvent(BladeDamage, Hit, BladeDirection,
			UUTDmgType_ScorpionBlade::StaticClass(), Momentum),
		GetVehicleDamageInstigator(), this);
}

void AUTVehicle_Scorpion::BreakBlade(bool bLeftBlade)
{
	if (Role != ROLE_Authority)
	{
		return;
	}

	if (bLeftBlade)
	{
		bLeftBladeBroken = true;
	}
	else
	{
		bRightBladeBroken = true;
	}
	if (bLeftBladeBroken && bRightBladeBroken)
	{
		bBladesExtended = false;
	}
	bHavePreviousBladePositions = false;
	OnRep_BladeState();
	ForceNetUpdate();
}

void AUTVehicle_Scorpion::RequestBoost()
{
	if (Role == ROLE_Authority)
	{
		StartBoost();
	}
	else
	{
		ServerStartBoost();
	}
}

bool AUTVehicle_Scorpion::ServerStartBoost_Validate()
{
	return true;
}

void AUTVehicle_Scorpion::ServerStartBoost_Implementation()
{
	StartBoost();
}

bool AUTVehicle_Scorpion::ServerSetNormalDriveThrottle_Validate(float NewThrottle)
{
	return FMath::IsFinite(NewThrottle) && NewThrottle >= -0.01f && NewThrottle <= 1.01f;
}

void AUTVehicle_Scorpion::ServerSetNormalDriveThrottle_Implementation(float NewThrottle)
{
	NormalDriveThrottle = FMath::Clamp(NewThrottle, 0.0f, 1.0f);
}

bool AUTVehicle_Scorpion::StartBoost()
{
	if (Role != ROLE_Authority || GetWorld() == nullptr || VehicleComponent == nullptr ||
		!VehicleComponent->HasDriver() || VehicleComponent->bDead || bSelfDestructArmed ||
		bBoostersActivated || GetWorld()->GetTimeSeconds() < NextBoostTime)
	{
		return false;
	}

	bBoostersActivated = true;
	BoostStartTime = GetWorld()->GetTimeSeconds();
	GetWorldTimerManager().SetTimer(BoostTimerHandle, this,
		&AUTVehicle_Scorpion::StopBoost, MaxBoostDuration, false);
	OnRep_BoostState();
	ForceNetUpdate();
	return true;
}

void AUTVehicle_Scorpion::StopBoost()
{
	if (Role != ROLE_Authority || bSelfDestructArmed || !bBoostersActivated)
	{
		return;
	}

	bBoostersActivated = false;
	if (GetWorld() != nullptr)
	{
		NextBoostTime = GetWorld()->GetTimeSeconds() + BoostRechargeDuration;
	}
	OnRep_BoostState();
	ForceNetUpdate();
}

void AUTVehicle_Scorpion::ApplyBoostForce()
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (Mesh == nullptr || !Mesh->IsSimulatingPhysics())
	{
		return;
	}

	const FVector Forward = GetActorForwardVector();
	const float ForwardSpeed = FVector::DotProduct(GetVelocity(), Forward);
	if (ForwardSpeed < BoostTargetSpeed)
	{
		const float SpeedAlpha = FMath::Clamp((BoostTargetSpeed - ForwardSpeed) / 1000.0f, 0.15f, 1.0f);
		Mesh->AddForce(Forward * BoostAcceleration * SpeedAlpha, NAME_None, true);
	}
}

void AUTVehicle_Scorpion::ApplyNormalDriveAssist()
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (Mesh == nullptr || !Mesh->IsSimulatingPhysics() || VehicleComponent == nullptr ||
		!VehicleComponent->HasDriver() || NormalDriveThrottle <= 0.1f)
	{
		return;
	}

	// Live telemetry proved that gearing changes alone simply traded wheel torque
	// for RPM and still settled near 1400 uu/s. Supply the missing arcade thrust
	// directly, tapering it to zero at the intended non-boosted top speed.
	const FVector Forward = GetActorForwardVector();
	const float ForwardSpeed = FVector::DotProduct(GetVelocity(), Forward);
	if (ForwardSpeed < NormalDriveTargetSpeed)
	{
		const float SpeedAlpha = FMath::Clamp(
			(NormalDriveTargetSpeed - ForwardSpeed) / 900.0f, 0.2f, 1.0f);
		Mesh->AddForce(Forward * NormalDriveAcceleration * NormalDriveThrottle * SpeedAlpha,
			NAME_None, true);
	}
}

bool AUTVehicle_Scorpion::ReadyToSelfDestruct() const
{
	return Role == ROLE_Authority && IsBoostEjectReady();
}

bool AUTVehicle_Scorpion::IsBoostEjectReady() const
{
	return GetWorld() != nullptr && VehicleComponent != nullptr &&
		VehicleComponent->HasDriver() && !VehicleComponent->bDead && bBoostersActivated &&
		!bSelfDestructArmed;
}

bool AUTVehicle_Scorpion::ShouldShowBoostEjectPrompt() const
{
	const bool bLocallyOccupied = Controller != nullptr ||
		(VehicleComponent != nullptr && VehicleComponent->HasDriver());
	return VehicleComponent != nullptr && bLocallyOccupied &&
		!VehicleComponent->bDead && bBoostersActivated && !bSelfDestructArmed;
}

bool AUTVehicle_Scorpion::ArmSelfDestructAndEject()
{
	if (!ReadyToSelfDestruct() || VehicleComponent == nullptr)
	{
		return false;
	}

	SelfDestructInstigator = GetVehicleDamageInstigator();
	bSelfDestructArmed = true;
	bBladesExtended = false;
	VehicleComponent->SetEntryLocked(true);
	GetWorldTimerManager().ClearTimer(BoostTimerHandle);

	// Stock JumpBoots add 1500 uu/s. UT3-style boost-eject is deliberately much
	// more violent: launch at exactly twice that impulse. Do not inherit the
	// Scorpion's planar velocity; LaunchCharacter overwrites X/Y with zero so the
	// driver leaves vertically at the instant boost-eject is pressed.
	if (!VehicleComponent->EjectDriver(FVector(0.0f, 0.0f, 3000.0f), false))
	{
		bSelfDestructArmed = false;
		VehicleComponent->SetEntryLocked(false);
		SelfDestructInstigator = nullptr;
		GetWorldTimerManager().SetTimer(BoostTimerHandle, this,
			&AUTVehicle_Scorpion::StopBoost,
			FMath::Max(0.01f, MaxBoostDuration - (GetWorld()->GetTimeSeconds() - BoostStartTime)), false);
		return false;
	}

	OnRep_BladeState();
	OnRep_SelfDestructArmed();
	GetWorldTimerManager().SetTimer(SelfDestructTimerHandle, this,
		&AUTVehicle_Scorpion::SelfDestructTimerExpired, SelfDestructFuseDuration, false);
	ForceNetUpdate();
	return true;
}

void AUTVehicle_Scorpion::OnRep_BoostState()
{
	if (Role < ROLE_Authority && bBoostersActivated && !bSelfDestructArmed && GetWorld() != nullptr)
	{
		// BoostStartTime is an authority timer. Give the owning client's HUD the
		// same short arming delay from the moment replicated boost begins.
		BoostStartTime = GetWorld()->GetTimeSeconds();
	}
	UE_LOG(LogTemp, Warning, TEXT("[ScorpionBoost] Rep Vehicle=%s Role=%d Local=%d Active=%d Armed=%d Speed=%.1f Prompt=%d Ready=%d"),
		*GetName(), (int32)Role, IsLocallyControlled() ? 1 : 0,
		bBoostersActivated ? 1 : 0, bSelfDestructArmed ? 1 : 0,
		GetVelocity().Size(), ShouldShowBoostEjectPrompt() ? 1 : 0,
		IsBoostEjectReady() ? 1 : 0);
	const float NewFOV = (bBoostersActivated || bSelfDestructArmed) ? 105.0f : 90.0f;
	if (Camera != nullptr)
	{
		Camera->SetFieldOfView(NewFOV);
	}
	if (ACameraActor* CameraActor = Cast<ACameraActor>(VehicleCameraActor))
	{
		if (CameraActor->GetCameraComponent() != nullptr)
		{
			CameraActor->GetCameraComponent()->SetFieldOfView(NewFOV);
		}
	}
}

void AUTVehicle_Scorpion::OnRep_SelfDestructArmed()
{
	OnRep_BoostState();
	RefreshBladeVisuals();
}

void AUTVehicle_Scorpion::OnArmedVehicleHit(UPrimitiveComponent*, AActor* OtherActor,
	UPrimitiveComponent*, FVector NormalImpulse, const FHitResult&)
{
	if (Role != ROLE_Authority || !bSelfDestructArmed || bHasSelfDestructed || OtherActor == this)
	{
		return;
	}
	if (SelfDestructInstigator != nullptr && OtherActor == SelfDestructInstigator->GetPawn())
	{
		return;
	}

	const float ImpactSpeed = GetVelocity().Size();
	if (Cast<APawn>(OtherActor) != nullptr || ImpactSpeed >= 900.0f || NormalImpulse.Size() >= 100000.0f)
	{
		SelfDestruct(OtherActor);
	}
}

void AUTVehicle_Scorpion::SelfDestructTimerExpired()
{
	SelfDestruct(nullptr);
}

void AUTVehicle_Scorpion::MulticastPlaySelfDestructEffects_Implementation(
	FVector EffectLocation, FRotator EffectRotation, FVector CanopyLocation,
	FRotator CanopyRotation, FVector InheritedVelocity)
{
	UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (SelfDestructEffect != nullptr)
	{
		const FTransform ImportedEffectTransform(EffectRotation, EffectLocation,
			FVector(SelfDestructEffectScale));
		UGameplayStatics::SpawnEmitterAtLocation(World, SelfDestructEffect,
			ImportedEffectTransform, true);
	}

	if (SelfDestructBlastEffect != nullptr)
	{
		// The imported UT3 system supplies the directional fire and smoke, but its
		// old authored scale reads as a tiny flare in UT4. Layer the proven grenade
		// launcher's full flash/fireball/smoke/audio effect beneath it. Scale is
		// carried by the transform; UT4's radius-parameter constructor is not
		// exported from the UnrealTournament module and cannot be linked here.
		const FTransform BlastTransform(FRotator::ZeroRotator,
			EffectLocation + FVector(0.0f, 0.0f, 30.0f), FVector(SelfDestructBlastScale));
		SelfDestructBlastEffect.GetDefaultObject()->SpawnEffect(World, BlastTransform,
			nullptr, this, nullptr, SRT_None);
	}

	if (SelfDestructCanopyMesh == nullptr)
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.Instigator = this;
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* CanopyActor = World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(), CanopyLocation, CanopyRotation, SpawnParameters);
	if (CanopyActor == nullptr)
	{
		return;
	}

	// The multicast is the replication mechanism. Keep the short-lived debris
	// local so it consumes no replicated actor or movement bandwidth.
	CanopyActor->SetReplicates(false);
	CanopyActor->SetMobility(EComponentMobility::Movable);
	CanopyActor->SetLifeSpan(SelfDestructCanopyLifetime);
	UStaticMeshComponent* CanopyComponent = CanopyActor->GetStaticMeshComponent();
	if (CanopyComponent != nullptr)
	{
		CanopyComponent->SetStaticMesh(SelfDestructCanopyMesh);
		CanopyComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		CanopyComponent->SetCollisionObjectType(ECC_PhysicsBody);
		CanopyComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
		CanopyComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		CanopyComponent->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
		CanopyComponent->SetSimulatePhysics(true);

		const FQuat LaunchRotation = EffectRotation.Quaternion();
		CanopyComponent->SetPhysicsLinearVelocity(InheritedVelocity +
			LaunchRotation.RotateVector(SelfDestructCanopyLaunchVelocity), false);
		CanopyComponent->SetPhysicsAngularVelocity(
			LaunchRotation.RotateVector(SelfDestructCanopyAngularVelocity), false);
	}
}

void AUTVehicle_Scorpion::SelfDestruct(AActor* ImpactedActor)
{
	if (Role != ROLE_Authority || bHasSelfDestructed)
	{
		return;
	}

	bHasSelfDestructed = true;
	GetWorldTimerManager().ClearTimer(BoostTimerHandle);
	GetWorldTimerManager().ClearTimer(SelfDestructTimerHandle);

	AController* DamageInstigator = SelfDestructInstigator != nullptr
		? SelfDestructInstigator
		: GetVehicleDamageInstigator();
	const FVector Origin = GetActorLocation();
	const FVector InheritedVelocity = GetVelocity();
	FRotator EffectRotation = GetActorRotation();
	FVector CanopyLocation = Origin;
	FRotator CanopyRotation = EffectRotation;
	USkeletalMeshComponent* ScorpionMesh = GetMesh();
	if (ScorpionMesh != nullptr)
	{
		EffectRotation = ScorpionMesh->GetComponentRotation();
		if (ScorpionMesh->GetBoneIndex(SelfDestructCanopyBone) != INDEX_NONE)
		{
			const FTransform CanopyTransform = ScorpionMesh->GetSocketTransform(
				SelfDestructCanopyBone, RTS_World);
			CanopyLocation = CanopyTransform.GetLocation();
			CanopyRotation = CanopyTransform.Rotator();
		}
	}
	TArray<AActor*> IgnoreActors;
	APawn* InstigatorPawn = DamageInstigator != nullptr ? DamageInstigator->GetPawn() : nullptr;
	if (InstigatorPawn != nullptr)
	{
		// UT3's self-destruct damage type explicitly does not hurt the driver who
		// armed it; the launched character can still be inside the 600 uu radius.
		IgnoreActors.Add(InstigatorPawn);
	}
	if (ImpactedActor != nullptr)
	{
		if (ImpactedActor != InstigatorPawn)
		{
			const FVector ImpactDirection = (ImpactedActor->GetActorLocation() - Origin).GetSafeNormal();
			const FHitResult ImpactHit(ImpactedActor, nullptr, ImpactedActor->GetActorLocation(), -ImpactDirection);
			ImpactedActor->TakeDamage(SelfDestructDamage,
				FUTPointDamageEvent(SelfDestructDamage, ImpactHit, ImpactDirection,
					UUTDmgType_ScorpionSelfDestruct::StaticClass(), ImpactDirection * SelfDestructMomentum),
				DamageInstigator, this);
			IgnoreActors.AddUnique(ImpactedActor);
		}
	}

	UUTGameplayStatics::UTHurtRadius(this, SelfDestructDamage, 0.0f, SelfDestructMomentum,
		Origin, 0.0f, SelfDestructRadius, 1.0f,
		UUTDmgType_ScorpionSelfDestruct::StaticClass(), IgnoreActors, this, DamageInstigator);
	MulticastPlaySelfDestructEffects(Origin, EffectRotation, CanopyLocation,
		CanopyRotation, InheritedVelocity);

	bBoostersActivated = false;
	bSelfDestructArmed = false;
	if (VehicleComponent != nullptr)
	{
		VehicleComponent->Health = 0;
		if (!VehicleComponent->bDead)
		{
			VehicleComponent->VehicleDied(DamageInstigator);
		}
	}
	SetActorEnableCollision(false);
	if (GetMesh() != nullptr)
	{
		GetMesh()->SetSimulatePhysics(false);
		GetMesh()->SetVisibility(false, true);
	}
	// Keep the torn-off source actor around long enough for the reliable cosmetic
	// multicast to reach every relevant client; the mesh is already hidden and
	// the independently spawned explosion components finish on their own.
	SetLifeSpan(2.0f);
	ForceNetUpdate();
}

AController* AUTVehicle_Scorpion::GetVehicleDamageInstigator() const
{
	if (Controller != nullptr)
	{
		return Controller;
	}
	return VehicleComponent != nullptr ? VehicleComponent->DamageInstigator : nullptr;
}

float AUTVehicle_Scorpion::TakeDamage(float Damage, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	const bool bWasArmed = bSelfDestructArmed;
	float ResolvedDamage = Damage;
	if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
	{
		ResolvedDamage = InternalTakeRadialDamage(Damage,
			static_cast<const FRadialDamageEvent&>(DamageEvent), EventInstigator, DamageCauser);
	}
	else if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		ResolvedDamage = InternalTakePointDamage(Damage,
			static_cast<const FPointDamageEvent&>(DamageEvent), EventInstigator, DamageCauser);
	}

	// Apply the boost vulnerability only after radial falloff, then let the
	// shared component apply the weapon's UT3 vehicle-damage multiplier.
	ResolvedDamage *= bSelfDestructArmed ? ArmedDamageMultiplier
		: (bBoostersActivated ? BoostDamageMultiplier : 1.0f);
	const float AppliedDamage = VehicleComponent != nullptr
		? VehicleComponent->ApplyDamage(ResolvedDamage, DamageEvent, EventInstigator, DamageCauser)
		: 0.0f;
	if (Role == ROLE_Authority && bWasArmed && AppliedDamage > 0.0f &&
		VehicleComponent != nullptr && VehicleComponent->bDead && !bHasSelfDestructed)
	{
		SelfDestruct(nullptr);
	}
	else if (Role == ROLE_Authority && VehicleComponent != nullptr && VehicleComponent->bDead)
	{
		StopBoost();
		SetBladesExtended(false);
	}
	return AppliedDamage;
}

// ---- Wheel classes ----

UUTWheel_Scorpion_Front::UUTWheel_Scorpion_Front()
{
	static ConstructorHelpers::FObjectFinder<UTireConfig> TireFinder(
		TEXT("/Game/Mogno/Vehicles/ScorpionTest/DataConfig_ScorpionTyre"));
	if (TireFinder.Succeeded())
	{
		TireConfig = TireFinder.Object;
	}

	ShapeRadius = 45.0f;
	ShapeWidth = 30.0f;
	Mass = 20.0f;
	DampingRate = 0.25f;
	SteerAngle = 30.0f;
	bAffectedByHandbrake = false;

	// Tire grip
	LatStiffMaxLoad = 2.0f;
	LatStiffValue = 18.0f;
	LongStiffValue = 1400.0f;

	// The chassis bottom is 19.5 uu above the tire contact at rest. More than
	// 15 uu compression therefore puts chassis collision through the floor.
	// Apply tire/spring forces nearer the COM to reduce squat and body roll.
	SuspensionForceOffset = -30.0f;
	SuspensionMaxRaise = 15.0f;
	// 15 compression + 22 droop gives the 37 uu locked travel from UT3 and
	// prevents the old 36 uu nose extension that made the tail hit the floor.
	SuspensionMaxDrop = 22.0f;
	SuspensionNaturalFrequency = 10.5f;
	SuspensionDampingRatio = 1.25f;

	// Brakes
	MaxBrakeTorque = 6000.0f;
	MaxHandBrakeTorque = 0.0f;
}

UUTWheel_Scorpion_Rear::UUTWheel_Scorpion_Rear()
{
	static ConstructorHelpers::FObjectFinder<UTireConfig> TireFinder(
		TEXT("/Game/Mogno/Vehicles/ScorpionTest/DataConfig_ScorpionTyre"));
	if (TireFinder.Succeeded())
	{
		TireConfig = TireFinder.Object;
	}

	ShapeRadius = 45.0f;
	ShapeWidth = 30.0f;
	Mass = 20.0f;
	DampingRate = 0.25f;
	SteerAngle = 0.0f;
	bAffectedByHandbrake = true;

	// Tire grip
	LatStiffMaxLoad = 2.0f;
	LatStiffValue = 18.0f;
	LongStiffValue = 1400.0f;

	// Match the front axle's bounded, critically controlled suspension.
	SuspensionForceOffset = -30.0f;
	SuspensionMaxRaise = 15.0f;
	SuspensionMaxDrop = 22.0f;
	SuspensionNaturalFrequency = 10.5f;
	SuspensionDampingRatio = 1.25f;

	// Brakes
	MaxBrakeTorque = 5000.0f;
	MaxHandBrakeTorque = 8000.0f;
}
