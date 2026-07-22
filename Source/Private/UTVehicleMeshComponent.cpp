#include "UTVehicleMeshComponent.h"
#include "WheeledVehicle.h"
#include "WheeledVehicleMovementComponent.h"
#include "VehicleWheel.h"
#include "Engine/SkeletalMesh.h"

namespace
{
	void ResetBoneAndDescendantsToReferencePose(USkeletalMesh* SkeletalMesh,
		TArray<FTransform>& ComponentPose, int32 RootBoneIndex)
	{
		if (SkeletalMesh == nullptr || !ComponentPose.IsValidIndex(RootBoneIndex))
		{
			return;
		}

		for (int32 BoneIndex = RootBoneIndex; BoneIndex < ComponentPose.Num(); ++BoneIndex)
		{
			if (BoneIndex == RootBoneIndex ||
				SkeletalMesh->RefSkeleton.BoneIsChildOf(BoneIndex, RootBoneIndex))
			{
				ComponentPose[BoneIndex] = FTransform(
					SkeletalMesh->GetComposedRefPoseMatrix(
						SkeletalMesh->RefSkeleton.GetBoneName(BoneIndex)));
				ComponentPose[BoneIndex].NormalizeRotation();
			}
		}
	}

	void RebaseBoneAndDescendants(USkeletalMesh* SkeletalMesh, TArray<FTransform>& ComponentPose,
		int32 RootBoneIndex, const FQuat& RotationDelta, bool bLocalSpace)
	{
		if (SkeletalMesh == nullptr || !ComponentPose.IsValidIndex(RootBoneIndex))
		{
			return;
		}

		const FTransform OriginalRootTransform = ComponentPose[RootBoneIndex];
		FTransform RotatedRootTransform = OriginalRootTransform;
		RotatedRootTransform.SetRotation(bLocalSpace
			? OriginalRootTransform.GetRotation() * RotationDelta
			: RotationDelta * OriginalRootTransform.GetRotation());
		RotatedRootTransform.NormalizeRotation();

		// Component-space descendants have already been evaluated. Rebase every
		// child onto the rotated root so barrels, sockets, and attached pieces move
		// as one rigid turret instead of leaving the muzzle at the reference pose.
		for (int32 BoneIndex = RootBoneIndex + 1; BoneIndex < ComponentPose.Num(); ++BoneIndex)
		{
			if (SkeletalMesh->RefSkeleton.BoneIsChildOf(BoneIndex, RootBoneIndex))
			{
				const FTransform RelativeToRoot = ComponentPose[BoneIndex].GetRelativeTransform(
					OriginalRootTransform);
				ComponentPose[BoneIndex] = RelativeToRoot * RotatedRootTransform;
				ComponentPose[BoneIndex].NormalizeRotation();
			}
		}
		ComponentPose[RootBoneIndex] = RotatedRootTransform;
	}
}

UUTVehicleMeshComponent::UUTVehicleMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bApplyWheelPose(true)
	, VisualWheelCenterOffsetZ(0.0f)
	, WeaponYawBoneName(NAME_None)
	, WeaponPitchBoneName(NAME_None)
	, WeaponAimRotation(FRotator::ZeroRotator)
	, bLoggedActiveWheelPose(false)
{
}

void UUTVehicleMeshComponent::SetWeaponAimRotation(const FRotator& NewAimRotation)
{
	WeaponAimRotation = NewAimRotation;
	WeaponAimRotation.Roll = 0.0f;
	WeaponAimRotation.Normalize();
}

void UUTVehicleMeshComponent::RefreshBoneTransforms(FActorComponentTickFunction* TickFunction)
{
	// Force the stock evaluation to finish on the game thread. We must apply the
	// wheel transforms after its physics blend or the disabled UT3 tire bodies
	// overwrite them with their reference pose.
	Super::RefreshBoneTransforms(nullptr);

	AWheeledVehicle* Vehicle = Cast<AWheeledVehicle>(GetOwner());
	UWheeledVehicleMovementComponent* Movement = Vehicle != nullptr
		? Vehicle->GetVehicleMovementComponent()
		: nullptr;
	if (SkeletalMesh == nullptr)
	{
		return;
	}

	const bool bHasWheelPose = bApplyWheelPose && Movement != nullptr && Movement->Wheels.Num() > 0 &&
		Movement->IsActive();
	if (bApplyWheelPose && Movement != nullptr && Movement->Wheels.Num() > 0 && !Movement->IsActive())
	{
		// The Goliath's spawn parking lock teleports the chassis to its corrected
		// ground height and then pauses vehicle simulation. Wheel->Location still
		// contains the pre-teleport PhysX result while paused; applying it here
		// visually leaves the rollers behind on the floor. Super has already
		// restored the authored reference pose, so keep that assembled parked pose
		// until entry reactivates the movement component and refreshes wheel data.
		bLoggedActiveWheelPose = false;
	}
	const bool bHasWeaponPose = WeaponYawBoneName != NAME_None ||
		WeaponPitchBoneName != NAME_None;
	if (!bHasWheelPose && !bHasWeaponPose)
	{
		return;
	}

	const TArray<FTransform>& CurrentPose = GetComponentSpaceTransforms();
	TArray<FTransform>& WheelPose = GetEditableComponentSpaceTransforms();
	if (&CurrentPose != &WheelPose)
	{
		WheelPose = CurrentPose;
	}

	int32 AppliedWheels = 0;
	if (bHasWheelPose)
	{
		for (int32 WheelIndex = 0; WheelIndex < Movement->Wheels.Num(); ++WheelIndex)
		{
			if (!Movement->WheelSetups.IsValidIndex(WheelIndex))
			{
				continue;
			}

			UVehicleWheel* Wheel = Movement->Wheels[WheelIndex];
			const int32 BoneIndex = GetBoneIndex(Movement->WheelSetups[WheelIndex].BoneName);
			if (Wheel == nullptr || !WheelPose.IsValidIndex(BoneIndex))
			{
				continue;
			}

			FTransform& WheelTransform = WheelPose[BoneIndex];
			const FQuat WheelRotation(FRotator(
				Wheel->GetRotationAngle(),
				Wheel->GetSteerAngle(),
				0.0f));
			WheelTransform.SetRotation(WheelRotation * WheelTransform.GetRotation());

			// AdditionalOffset moves the PhysX wheel away from its skeletal bone, but
			// the stock vehicle animation only applies suspension jounce to the bone.
			// The Scorpion needs a large mesh-specific AdditionalOffset, so that path
			// leaves the rendered tire at a different height from the wheel that is
			// actually supporting the chassis. Drive the visual suspension axis from
			// PhysX's wheel-shape center instead. Keep the authored X/Y so steering and
			// the UT3 tire spacing remain unchanged.
			FVector VisualLocation = WheelTransform.GetTranslation();
			const FVector PhysicsLocation = GetComponentTransform().InverseTransformPosition(Wheel->Location);
			// The imported UT3 render tire is slightly larger than its PhysX wheel
			// shape. A mesh-specific center lift keeps the visible rubber on the floor
			// without changing gearing, suspension load, or contact physics.
			VisualLocation.Z = PhysicsLocation.Z + VisualWheelCenterOffsetZ;
			WheelTransform.SetTranslation(VisualLocation);
			WheelTransform.NormalizeRotation();
			++AppliedWheels;
		}
	}

	const int32 YawBoneIndex = GetBoneIndex(WeaponYawBoneName);
	const int32 PitchBoneIndex = GetBoneIndex(WeaponPitchBoneName);
	bool bAppliedWeaponPose = false;
	if (WheelPose.IsValidIndex(YawBoneIndex))
	{
		// RefreshBoneTransforms() can preserve this component's previous edited
		// component-space buffer when no AnimInstance drives the imported mesh.
		// Reset the whole rigid turret tree before applying today's absolute aim;
		// otherwise the full yaw/pitch is added again every frame and it spins.
		ResetBoneAndDescendantsToReferencePose(SkeletalMesh, WheelPose, YawBoneIndex);
		bAppliedWeaponPose = true;
	}
	else if (WheelPose.IsValidIndex(PitchBoneIndex))
	{
		ResetBoneAndDescendantsToReferencePose(SkeletalMesh, WheelPose, PitchBoneIndex);
		bAppliedWeaponPose = true;
	}

	if (WheelPose.IsValidIndex(YawBoneIndex) && !FMath::IsNearlyZero(WeaponAimRotation.Yaw))
	{
		RebaseBoneAndDescendants(SkeletalMesh, WheelPose, YawBoneIndex,
			FQuat(FRotator(0.0f, WeaponAimRotation.Yaw, 0.0f)), false);
	}
	if (WheelPose.IsValidIndex(PitchBoneIndex) && !FMath::IsNearlyZero(WeaponAimRotation.Pitch))
	{
		RebaseBoneAndDescendants(SkeletalMesh, WheelPose, PitchBoneIndex,
			FQuat(FRotator(WeaponAimRotation.Pitch, 0.0f, 0.0f)), true);
	}

	if (AppliedWheels > 0 || bAppliedWeaponPose)
	{
		bNeedToFlipSpaceBaseBuffers = true;
		FinalizeBoneTransform();
		MarkRenderDynamicDataDirty();

		if (AppliedWheels > 0 && !bLoggedActiveWheelPose)
		{
			bLoggedActiveWheelPose = true;
			UE_LOG(LogTemp, Warning, TEXT("[VehicleAnimation] Native wheel pose active Mesh=%s Wheels=%d VisualLiftZ=%.1f"),
				*GetName(), AppliedWheels, VisualWheelCenterOffsetZ);
		}
	}
}
