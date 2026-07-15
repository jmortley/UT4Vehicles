#include "UTVehicle_Scorpion.h"
#include "UnrealTournament.h"
#include "WheeledVehicleMovementComponent4W.h"
#include "UTVehicleComponent.h"
#include "UTVehicleMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "TireConfig.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"

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

		// The supplied Scorpion tire is 2.0 by default, which still lets the
		// higher-output UT4-scale setup skate. This asset is Scorpion-specific,
		// so raise its combined longitudinal/lateral grip before PhysX rebuilds.
		UTireConfig* ScorpionTire = UUTWheel_Scorpion_Front::StaticClass()->GetDefaultObject<UUTWheel_Scorpion_Front>()->TireConfig;
		if (ScorpionTire != nullptr)
		{
			ScorpionTire->SetFrictionScale(3.5f);
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
		TorqueCurve->AddKey(0.0f, 500.0f);
		TorqueCurve->AddKey(3225.5f, 500.0f);
		TorqueCurve->AddKey(3565.1f, 80.0f);
		TorqueCurve->AddKey(3904.6f, 0.0f);
		TorqueCurve->AddKey(4000.0f, 0.0f);

		Movement4W->TransmissionSetup.bUseGearAutoBox = true;
		Movement4W->TransmissionSetup.ClutchStrength = 10.0f;
		Movement4W->TransmissionSetup.NeutralGearUpRatio = 0.15f;
		Movement4W->TransmissionSetup.GearSwitchTime = 0.15f;
		Movement4W->TransmissionSetup.GearAutoBoxLatency = 2.0f;
		// 4:1 first gear x 2:1 final drive = 8:1 overall. A 45 cm wheel then
		// reaches about 2350 uu/s at the 4000 RPM limiter.
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

		FRichCurve* SteeringCurve = Movement4W->SteeringCurve.GetRichCurve();
		SteeringCurve->Reset();
		// UE4's curve input is km/h. These are the original UT3 Scorpion steering
		// angles with the speed axis doubled for the new 2300 uu/s UT4 target.
		SteeringCurve->AddKey(0.0f, 1.0f);
		SteeringCurve->AddKey(43.2f, 0.333f);
		SteeringCurve->AddKey(79.2f, 0.222f);
		SteeringCurve->AddKey(93.6f, 0.133f);
		SteeringCurve->AddKey(115.2f, 0.022f);
	}
}

AUTVehicle_Scorpion::AUTVehicle_Scorpion(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
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

	// Vehicle component setup
	if (VehicleComponent != nullptr)
	{
		VehicleComponent->Health = 600;
		VehicleComponent->HealthMax = 600;
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

	// Mass override for arcade feel
	Movement4W->Mass = 1200.0f;

	// Lower center of mass so the vehicle doesn't topple.
	// COMNudge shifts the COM relative to the physics body center. Negative Z = lower.
	GetMesh()->BodyInstance.COMNudge = FVector(0.0f, 0.0f, -80.0f);

	// Resist rolling via inertia tensor scaling (higher Y/Z = harder to tip)
	Movement4W->InertiaTensorScale = FVector(1.0f, 1.333f, 1.333f);

	// Note: ThrottleInputRate, BrakeInputRate, SteeringInputRate are protected
	// in UWheeledVehicleMovementComponent. Tune via Blueprint defaults if needed.
}

void AUTVehicle_Scorpion::PostInitializeComponents()
{
	Super::PostInitializeComponents();

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
		UE_LOG(LogTemp, Warning, TEXT("[VehiclePhysics] Applied Scorpion drive setup Physics=%d ChassisBone=%s ChassisZ=(%.2f..%.2f) WheelOffsetZ=(%.2f,%.2f,%.2f,%.2f) MeshClass=%s Tire=%s Friction=%.2f FinalRatio=%.2f PeakTorque=%.1f Diff=%d FrontSplit=%.2f"),
			Movement4W->HasValidPhysicsState() ? 1 : 0,
			*ChassisBoneName,
			ChassisBounds.IsValid ? ChassisBounds.Min.Z : 0.0f,
			ChassisBounds.IsValid ? ChassisBounds.Max.Z : 0.0f,
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
			Movement4W->DifferentialSetup.FrontRearSplit);
	}
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
	SteerAngle = 40.0f;
	bAffectedByHandbrake = false;

	// Tire grip
	LatStiffMaxLoad = 2.0f;
	LatStiffValue = 25.0f;
	LongStiffValue = 1500.0f;

	// Suspension - generous travel for UT3 mesh scale
	SuspensionForceOffset = 0.0f;
	SuspensionMaxRaise = 40.0f;
	SuspensionMaxDrop = 40.0f;
	SuspensionNaturalFrequency = 9.0f;
	SuspensionDampingRatio = 1.5f;

	// Brakes
	MaxBrakeTorque = 3000.0f;
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
	LatStiffValue = 25.0f;
	LongStiffValue = 1500.0f;

	// Suspension - generous travel for UT3 mesh scale
	SuspensionForceOffset = 0.0f;
	SuspensionMaxRaise = 40.0f;
	SuspensionMaxDrop = 40.0f;
	SuspensionNaturalFrequency = 9.0f;
	SuspensionDampingRatio = 1.5f;

	// Brakes
	MaxBrakeTorque = 2000.0f;
	MaxHandBrakeTorque = 6000.0f;
}
