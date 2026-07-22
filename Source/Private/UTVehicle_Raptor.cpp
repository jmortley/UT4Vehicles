#include "UTVehicle_Raptor.h"
#include "UnrealTournament.h"
#include "UTVehicleComponent.h"
#include "UTVehicleMovementHover.h"
#include "Engine/SkeletalMesh.h"

namespace
{
	void ApplyRaptorWingFold(USkeletalMesh* SkeletalMesh, TArray<FTransform>& ComponentPose,
		FName WingBoneName, float FoldAngleDegrees)
	{
		if (SkeletalMesh == nullptr || FMath::IsNearlyZero(FoldAngleDegrees))
		{
			return;
		}

		const int32 WingBoneIndex = SkeletalMesh->RefSkeleton.FindBoneIndex(WingBoneName);
		if (!ComponentPose.IsValidIndex(WingBoneIndex))
		{
			return;
		}

		const FTransform OriginalWingTransform = ComponentPose[WingBoneIndex];
		FTransform FoldedWingTransform = OriginalWingTransform;
		const FQuat FoldRotation(FRotator(FoldAngleDegrees, 0.0f, 0.0f));
		FoldedWingTransform.SetRotation(FoldRotation * OriginalWingTransform.GetRotation());
		FoldedWingTransform.NormalizeRotation();

		// Component-space descendants have already been evaluated. Rebase them onto
		// the folded root so damage bones, sockets, and collision follow it.
		for (int32 BoneIndex = WingBoneIndex + 1; BoneIndex < ComponentPose.Num(); ++BoneIndex)
		{
			if (SkeletalMesh->RefSkeleton.BoneIsChildOf(BoneIndex, WingBoneIndex))
			{
				const FTransform RelativeToWing = ComponentPose[BoneIndex].GetRelativeTransform(
					OriginalWingTransform);
				ComponentPose[BoneIndex] = RelativeToWing * FoldedWingTransform;
				ComponentPose[BoneIndex].NormalizeRotation();
			}
		}
		ComponentPose[WingBoneIndex] = FoldedWingTransform;
	}
}

UUTRaptorMeshComponent::UUTRaptorMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	WingFoldAlpha = 0.0f;
	WingFoldAngleDegrees = -70.0f;
}

void UUTRaptorMeshComponent::RefreshBoneTransforms(FActorComponentTickFunction* TickFunction)
{
	// Finish the stock/reference pose first, then reproduce UT3's actor-space
	// RaptorWing controller on the two imported wing roots.
	Super::RefreshBoneTransforms(nullptr);
	if (SkeletalMesh == nullptr || WingFoldAlpha <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const TArray<FTransform>& CurrentPose = GetComponentSpaceTransforms();
	TArray<FTransform>& ComponentPose = GetEditableComponentSpaceTransforms();
	if (&CurrentPose != &ComponentPose)
	{
		ComponentPose = CurrentPose;
	}
	const float AppliedAngle = WingFoldAngleDegrees * FMath::Clamp(WingFoldAlpha, 0.0f, 1.0f);
	ApplyRaptorWingFold(SkeletalMesh, ComponentPose, FName(TEXT("Lft_Wing")), AppliedAngle);
	ApplyRaptorWingFold(SkeletalMesh, ComponentPose, FName(TEXT("Rt_Wing")), AppliedAngle);

	bNeedToFlipSpaceBaseBuffers = true;
	FinalizeBoneTransform();
	MarkRenderDynamicDataDirty();
	// Keep the imported wing bodies and sockets on the compact pose as well as
	// the render mesh, which is what makes low passes physically narrower.
	UpdateKinematicBonesToAnim(GetComponentSpaceTransforms(), ETeleportType::None, true);
}

AUTVehicle_Raptor::AUTVehicle_Raptor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UUTRaptorMeshComponent>(TEXT("VehicleMesh")))
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

	WingFoldGroundDistance = 450.0f;
	WingUnfoldGroundDistance = 650.0f;
	WingFoldAngleDegrees = -70.0f;
	WingFoldRate = 2.5f;
	if (UUTRaptorMeshComponent* RaptorMesh = Cast<UUTRaptorMeshComponent>(VehicleMesh))
	{
		RaptorMesh->WingFoldAngleDegrees = WingFoldAngleDegrees;
	}

	// UT3 Raptor reference: AirSpeed=2500, forces 750 forward / 100 reverse /
	// 450 strafe / 500 rise, with 0.7 Long/Lat/Up damping. This variant raises
	// flight speed by 30 percent while preserving those force ratios and the same
	// asymptotic acceleration shape.
	if (HoverMovement != nullptr)
	{
		HoverMovement->MaxSpeed = 3250.0f;
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
		HoverMovement->ParkedHorizontalDeceleration = 3500.0f;
		HoverMovement->ParkedGroundSearchDistance = 20000.0f;
	}
}

void AUTVehicle_Raptor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateWingFold(DeltaSeconds);
}

void AUTVehicle_Raptor::UpdateWingFold(float DeltaSeconds)
{
	UUTRaptorMeshComponent* RaptorMesh = Cast<UUTRaptorMeshComponent>(VehicleMesh);
	if (RaptorMesh == nullptr || GetWorld() == nullptr)
	{
		return;
	}

	FHitResult GroundHit;
	FCollisionQueryParams QueryParams(FName(TEXT("RaptorWingGround")), false, this);
	const FVector TraceStart = GetActorLocation();
	const FVector TraceEnd = TraceStart - FVector(0.0f, 0.0f,
		FMath::Max(WingUnfoldGroundDistance + 100.0f, 1000.0f));
	const bool bGroundHit = GetWorld()->LineTraceSingleByChannel(GroundHit, TraceStart,
		TraceEnd, ECC_WorldStatic, QueryParams);
	const float GroundDistance = bGroundHit ? GroundHit.Distance : BIG_NUMBER;
	const float HeightThreshold = RaptorMesh->WingFoldAlpha > 0.05f
		? WingUnfoldGroundDistance : WingFoldGroundDistance;
	const bool bVacant = VehicleComponent == nullptr || !VehicleComponent->HasDriver() ||
		VehicleComponent->bDead;
	const bool bDescending = bLiftDown || GetVelocity().Z < -100.0f;
	const bool bShouldFold = bVacant || bDescending || GroundDistance <= HeightThreshold;

	RaptorMesh->WingFoldAngleDegrees = WingFoldAngleDegrees;
	RaptorMesh->WingFoldAlpha = FMath::FInterpConstantTo(RaptorMesh->WingFoldAlpha,
		bShouldFold ? 1.0f : 0.0f, DeltaSeconds, FMath::Max(WingFoldRate, 0.0f));
}
