#include "UTVehicleMeshComponent.h"
#include "WheeledVehicle.h"
#include "WheeledVehicleMovementComponent.h"
#include "VehicleWheel.h"

UUTVehicleMeshComponent::UUTVehicleMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, VisualWheelCenterOffsetZ(0.0f)
	, bLoggedActiveWheelPose(false)
{
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
	if (Movement == nullptr || SkeletalMesh == nullptr || Movement->Wheels.Num() == 0)
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

	if (AppliedWheels > 0)
	{
		bNeedToFlipSpaceBaseBuffers = true;
		FinalizeBoneTransform();
		MarkRenderDynamicDataDirty();

		if (!bLoggedActiveWheelPose)
		{
			bLoggedActiveWheelPose = true;
			UE_LOG(LogTemp, Warning, TEXT("[VehicleAnimation] Native wheel pose active Mesh=%s Wheels=%d VisualLiftZ=%.1f"),
				*GetName(), AppliedWheels, VisualWheelCenterOffsetZ);
		}
	}
}
