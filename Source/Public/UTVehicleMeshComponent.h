#pragma once

#include "Components/SkeletalMeshComponent.h"
#include "UTVehicleMeshComponent.generated.h"

/**
 * Vehicle mesh that applies the PhysX wheel pose after the normal skeletal
 * animation/physics pass. UE4.15 cannot instantiate a plain native
 * UVehicleAnimInstance through AnimClass, so code-only vehicles need to apply
 * the same rotation, steering, and suspension offsets here.
 */
UCLASS()
class UTVEHICLES_API UUTVehicleMeshComponent : public USkeletalMeshComponent
{
	GENERATED_UCLASS_BODY()

public:
	/** Mesh-space lift applied after matching a visual wheel to its PhysX center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Vehicle)
	float VisualWheelCenterOffsetZ;

	virtual void RefreshBoneTransforms(FActorComponentTickFunction* TickFunction = nullptr) override;

private:
	bool bLoggedActiveWheelPose;
};
