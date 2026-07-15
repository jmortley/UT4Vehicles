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

	// Fixed chase orientation keeps the boom behind the vehicle instead of
	// inheriting UT's character-oriented control camera. The Raptor mesh is
	// roughly 1770 uu long, so 1200 uu was not enough to frame the full craft.
	if (SpringArm != nullptr)
	{
		SpringArm->TargetArmLength = 1800.0f;
		SpringArm->SetRelativeLocation(FVector(100.0f, 0.0f, 475.0f));
		SpringArm->SetRelativeRotation(FRotator(-8.0f, 0.0f, 0.0f));
		SpringArm->TargetOffset = FVector::ZeroVector;
		SpringArm->bUsePawnControlRotation = false;
		SpringArm->bEnableCameraLag = true;
		SpringArm->CameraLagSpeed = 8.0f;
	}
	if (Camera != nullptr)
	{
		Camera->SetFieldOfView(90.0f);
	}

	// UT3 Raptor reference: AirSpeed=2500, forces 750 forward / 100 reverse /
	// 450 strafe / 500 rise, with 0.7 Long/Lat/Up damping. This kinematic port
	// preserves those ratios and derives forward acceleration from the desired
	// terminal speed (2500 * 0.7), giving the same asymptotic acceleration shape.
	if (HoverMovement != nullptr)
	{
		HoverMovement->MaxSpeed = 2500.0f;
		HoverMovement->MaxAltitude = 5000.0f;
		const float UT3ForceScale = (HoverMovement->MaxSpeed * 0.7f) / 750.0f;
		HoverMovement->ThrustForce = 750.0f * UT3ForceScale;
		HoverMovement->ReverseThrustForce = 100.0f * UT3ForceScale;
		HoverMovement->StrafeForce = 450.0f * UT3ForceScale;
		HoverMovement->LiftForce = 500.0f * UT3ForceScale;
		HoverMovement->GravityForce = 600.0f;
		HoverMovement->TurnRate = 150.0f;
		HoverMovement->PitchRate = 80.0f;
		HoverMovement->VelocityDamping = 0.7f;
		HoverMovement->StopThreshold = 100.0f;
		HoverMovement->MinHoverHeight = 150.0f;
		HoverMovement->HoverVerticalDamping = 8.0f;
		HoverMovement->ParkedGroundClearance = 60.0f;
		HoverMovement->ParkedDescentSpeed = 400.0f;
	}
}
