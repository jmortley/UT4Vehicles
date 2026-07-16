#pragma once

#include "UTVehicle.h"
#include "VehicleWheel.h"
#include "UTVehicle_Goliath.generated.h"

class AUTProj_TankShell;

/**
 * Native UT3 Goliath driver vehicle. The chassis is a four-contact PhysX
 * vehicle with native skid-steer yaw; the driver owns the main cannon.
 * Passenger weapons intentionally wait for the reusable seat system.
 */
UCLASS(Blueprintable)
class UTVEHICLES_API AUTVehicle_Goliath : public AUTVehicle
{
	GENERATED_UCLASS_BODY()

public:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Angular acceleration used for zero-radius and moving skid steering. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Goliath|Movement")
	float TankTurnAcceleration;

	/** Top pivot yaw rate in degrees per second. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Goliath|Movement")
	float TankPivotYawRate;

	/** Reduced yaw rate while the chassis is at top speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Goliath|Movement")
	float TankMovingYawRate;

	/** UT3 inside-track torque fraction, used to reduce high-speed turning. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Goliath|Movement")
	float InsideTrackTorqueFactor;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Goliath|Cannon")
	TSubclassOf<AUTProj_TankShell> TankShellClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Goliath|Cannon")
	FName CannonMuzzleSocket;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Goliath|Cannon")
	float CannonFireInterval;

protected:
	virtual void OnPrimaryFirePressed() override;
	virtual void OnPrimaryFireReleased() override;
	virtual bool HandleDriverLeaveRequest() override;

	UFUNCTION(Reliable, Server, WithValidation)
	void ServerSetCannonFiring(bool bNewFiring);

	void SetCannonFiring(bool bNewFiring);
	void FireCannon();
	void ApplyTankSteering();
	void UpdateVacantParking(float DeltaSeconds);
	bool FindParkingGround(float& OutGroundZ, float& OutParkedActorZ) const;
	float GetParkedHeightAboveGround() const;
	FRotator GetCannonAimRotation() const;

	bool bCannonFiring;
	/** Hard physics-response lock used only before this spawn receives its first driver. */
	bool bSpawnParkingLocked;
	float NextCannonFireTime;
	FTimerHandle CannonFireTimerHandle;
};

/** Goliath contact wheel; steering is supplied by chassis skid torque. */
UCLASS()
class UTVEHICLES_API UUTWheel_Goliath : public UVehicleWheel
{
	GENERATED_BODY()

public:
	UUTWheel_Goliath();
};
