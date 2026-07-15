#pragma once

#include "UTVehicleFlying.h"
#include "Components/SkeletalMeshComponent.h"
#include "UTVehicle_Raptor.generated.h"

/** Skeletal mesh component that reapplies the native UT3 wing controller pose. */
UCLASS()
class UTVEHICLES_API UUTRaptorMeshComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	UUTRaptorMeshComponent(const FObjectInitializer& ObjectInitializer);

	/** Zero is the reference flight pose; one is the compact swept-back pose. */
	float WingFoldAlpha;

	/** Actor-space pitch applied to both mirrored wing roots at full fold. */
	float WingFoldAngleDegrees;

	virtual void RefreshBoneTransforms(FActorComponentTickFunction* TickFunction = nullptr) override;
};

/**
 * UT3 Raptor - Flying attack vehicle.
 * Uses SK_VH_Raptor skeletal mesh from UT3 assets.
 * UT3-style flight controls: mouse aim/pitch, WASD thrust, Space/Ctrl altitude.
 */
UCLASS()
class UTVEHICLES_API AUTVehicle_Raptor : public AUTVehicleFlying
{
	GENERATED_UCLASS_BODY()

public:
	virtual void Tick(float DeltaSeconds) override;

	/** Wings begin sweeping back below this ground clearance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Raptor|Wings")
	float WingFoldGroundDistance;

	/** Hysteresis height required before low-flight wings open again. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Raptor|Wings")
	float WingUnfoldGroundDistance;

	/** Full actor-space wing pitch. UT3's controller uses the same -90 degree axis. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Raptor|Wings")
	float WingFoldAngleDegrees;

	/** Normalized fold-alpha travel per second. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Raptor|Wings")
	float WingFoldRate;

protected:
	void UpdateWingFold(float DeltaSeconds);
};
