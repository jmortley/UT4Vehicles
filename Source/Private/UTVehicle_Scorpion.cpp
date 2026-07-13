#include "UTVehicle_Scorpion.h"
#include "UnrealTournament.h"
#include "WheeledVehicleMovementComponent4W.h"
#include "UTVehicleComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"

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

	// Use the minimal chassis-only physics asset instead of the UT3 one.
	// The UT3 asset has bodies on every bone which fights with PhysX wheel sim.
	static ConstructorHelpers::FObjectFinder<UPhysicsAsset> PhysAssetFinder(
		TEXT("/Game/RestrictedAssets/Proto/UT3_Vehicles/VH_Scorpion/Meshes/SK_VH_Scorpion_001_Physics"));
	if (PhysAssetFinder.Succeeded())
	{
		GetMesh()->PhysicsAssetOverride = PhysAssetFinder.Object;
	}

	// Vehicle component setup
	if (VehicleComponent != nullptr)
	{
		VehicleComponent->Health = 600;
		VehicleComponent->HealthMax = 600;
		VehicleComponent->EntryRadius = 350.0f;
	}

	// Camera adjustments for Scorpion
	if (SpringArm != nullptr)
	{
		SpringArm->TargetArmLength = 500.0f;
		SpringArm->SetRelativeLocation(FVector(0.0f, 0.0f, 80.0f));
	}

	// 4W Movement Component setup
	UWheeledVehicleMovementComponent4W* Movement4W = CastChecked<UWheeledVehicleMovementComponent4W>(GetVehicleMovementComponent());

	// Wheel setup - 4 wheels
	// NOTE: Bone names must match the Scorpion skeleton. Common UT3 naming:
	// Tire_FrontLeft, Tire_FrontRight, Tire_RearLeft, Tire_RearRight
	// If these don't match, check the skeleton in the editor and update.
	Movement4W->WheelSetups.SetNum(4);

	Movement4W->WheelSetups[0].WheelClass = UUTWheel_Scorpion_Front::StaticClass();
	Movement4W->WheelSetups[0].BoneName = FName(TEXT("F_L_Tire"));
	Movement4W->WheelSetups[0].AdditionalOffset = FVector(0.0f);

	Movement4W->WheelSetups[1].WheelClass = UUTWheel_Scorpion_Front::StaticClass();
	Movement4W->WheelSetups[1].BoneName = FName(TEXT("F_R_Tire"));
	Movement4W->WheelSetups[1].AdditionalOffset = FVector(0.0f);

	Movement4W->WheelSetups[2].WheelClass = UUTWheel_Scorpion_Rear::StaticClass();
	Movement4W->WheelSetups[2].BoneName = FName(TEXT("R_L_Tire"));
	Movement4W->WheelSetups[2].AdditionalOffset = FVector(0.0f);

	Movement4W->WheelSetups[3].WheelClass = UUTWheel_Scorpion_Rear::StaticClass();
	Movement4W->WheelSetups[3].BoneName = FName(TEXT("R_R_Tire"));
	Movement4W->WheelSetups[3].AdditionalOffset = FVector(0.0f);

	// Engine setup - arcade-ish feel
	Movement4W->EngineSetup.MaxRPM = 5500.0f;
	Movement4W->EngineSetup.MOI = 1.0f;
	Movement4W->EngineSetup.DampingRateFullThrottle = 0.15f;
	Movement4W->EngineSetup.DampingRateZeroThrottleClutchEngaged = 2.0f;
	Movement4W->EngineSetup.DampingRateZeroThrottleClutchDisengaged = 0.35f;

	// Transmission setup
	Movement4W->TransmissionSetup.bUseGearAutoBox = true;
	Movement4W->TransmissionSetup.GearSwitchTime = 0.15f;
	Movement4W->TransmissionSetup.GearAutoBoxLatency = 1.0f;
	Movement4W->TransmissionSetup.FinalRatio = 4.0f;
	Movement4W->TransmissionSetup.ReverseGearRatio = 2.9f;

	// Forward gears
	Movement4W->TransmissionSetup.ForwardGears.SetNum(4);
	Movement4W->TransmissionSetup.ForwardGears[0].Ratio = 4.25f;
	Movement4W->TransmissionSetup.ForwardGears[0].DownRatio = 0.0f;
	Movement4W->TransmissionSetup.ForwardGears[0].UpRatio = 0.65f;
	Movement4W->TransmissionSetup.ForwardGears[1].Ratio = 2.52f;
	Movement4W->TransmissionSetup.ForwardGears[1].DownRatio = 0.3f;
	Movement4W->TransmissionSetup.ForwardGears[1].UpRatio = 0.65f;
	Movement4W->TransmissionSetup.ForwardGears[2].Ratio = 1.66f;
	Movement4W->TransmissionSetup.ForwardGears[2].DownRatio = 0.3f;
	Movement4W->TransmissionSetup.ForwardGears[2].UpRatio = 0.65f;
	Movement4W->TransmissionSetup.ForwardGears[3].Ratio = 1.23f;
	Movement4W->TransmissionSetup.ForwardGears[3].DownRatio = 0.3f;
	Movement4W->TransmissionSetup.ForwardGears[3].UpRatio = 1.0f;

	// Differential - rear wheel drive like a proper scorpion
	Movement4W->DifferentialSetup.DifferentialType = EVehicleDifferential4W::LimitedSlip_RearDrive;
	Movement4W->DifferentialSetup.FrontRearSplit = 0.35f;

	// Steering curve - reduce max steer at high speed
	Movement4W->SteeringCurve.GetRichCurve()->Reset();
	Movement4W->SteeringCurve.GetRichCurve()->AddKey(0.0f, 1.0f);
	Movement4W->SteeringCurve.GetRichCurve()->AddKey(40.0f, 0.7f);
	Movement4W->SteeringCurve.GetRichCurve()->AddKey(120.0f, 0.5f);

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

// ---- Wheel classes ----

UUTWheel_Scorpion_Front::UUTWheel_Scorpion_Front()
{
	ShapeRadius = 45.0f;
	ShapeWidth = 30.0f;
	Mass = 20.0f;
	DampingRate = 0.25f;
	SteerAngle = 40.0f;
	bAffectedByHandbrake = false;

	// Tire grip
	LatStiffMaxLoad = 2.0f;
	LatStiffValue = 17.0f;
	LongStiffValue = 1000.0f;

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
	ShapeRadius = 45.0f;
	ShapeWidth = 30.0f;
	Mass = 20.0f;
	DampingRate = 0.25f;
	SteerAngle = 0.0f;
	bAffectedByHandbrake = true;

	// Tire grip
	LatStiffMaxLoad = 2.0f;
	LatStiffValue = 17.0f;
	LongStiffValue = 1000.0f;

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
