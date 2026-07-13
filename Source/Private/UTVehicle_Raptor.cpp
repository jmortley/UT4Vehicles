#include "UTVehicle_Raptor.h"
#include "UnrealTournament.h"
#include "UTVehicleComponent.h"
#include "UTVehicleMovementHover.h"

AUTVehicle_Raptor::AUTVehicle_Raptor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Mesh setup
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshFinder(
		TEXT("/Game/RestrictedAssets/Proto/UT3_Vehicles/VH_Raptor/Meshes/SK_VH_Raptor"));
	if (MeshFinder.Succeeded() && VehicleMesh != nullptr)
	{
		VehicleMesh->SetSkeletalMesh(MeshFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(
		TEXT("/Game/RestrictedAssets/Proto/UT3_Vehicles/VH_Raptor/Materials/M_Raptor"));
	if (MatFinder.Succeeded() && VehicleMesh != nullptr)
	{
		VehicleMesh->SetMaterial(0, MatFinder.Object);
	}

	// Vehicle component setup
	if (VehicleComponent != nullptr)
	{
		VehicleComponent->Health = 800;
		VehicleComponent->HealthMax = 800;
		VehicleComponent->EntryRadius = 400.0f;
	}

	// Camera adjustments for Raptor - further back for flight
	if (SpringArm != nullptr)
	{
		SpringArm->TargetArmLength = 800.0f;
		SpringArm->SetRelativeLocation(FVector(0.0f, 0.0f, 200.0f));
	}

	// Flight tuning for Raptor - fast and agile
	if (HoverMovement != nullptr)
	{
		HoverMovement->MaxSpeed = 2500.0f;
		HoverMovement->MaxAltitude = 5000.0f;
		HoverMovement->ThrustForce = 3500.0f;
		HoverMovement->LiftForce = 3000.0f;
		HoverMovement->GravityForce = 600.0f;
		HoverMovement->TurnRate = 150.0f;
		HoverMovement->PitchRate = 80.0f;
		HoverMovement->VelocityDamping = 0.015f;
		HoverMovement->MinHoverHeight = 150.0f;
	}
}
