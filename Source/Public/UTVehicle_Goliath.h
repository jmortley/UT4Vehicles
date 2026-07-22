#pragma once

#include "UTVehicle.h"
#include "VehicleWheel.h"
#include "UTVehicle_Goliath.generated.h"

class AUTProj_TankShell;
class UParticleSystem;
class USoundBase;

/**
 * Native UT3 Goliath driver vehicle. The chassis keeps a four-contact PhysX
 * vehicle for engine/input replication, while native tracked drive and
 * skid-steer forces move the rigid chassis; the driver owns the main cannon.
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
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

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

	/** Native tracked-drive top speed; independent of decorative wheel contact. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Goliath|Movement")
	float TankMaxForwardSpeed;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Goliath|Movement")
	float TankMaxReverseSpeed;

	/** Grounded forward/reverse acceleration in Unreal units per second squared. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Goliath|Movement")
	float TankDriveAcceleration;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Goliath|Movement")
	float TankBrakeDeceleration;

	/** Track-like resistance to sideways chassis sliding while grounded. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Goliath|Movement")
	float TankLateralDamping;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Goliath|Cannon")
	TSubclassOf<AUTProj_TankShell> TankShellClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Goliath|Cannon")
	FName CannonMuzzleSocket;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Goliath|Cannon")
	float CannonFireInterval;

	/** Original UT3 constrained-turret visual rate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Goliath|Cannon")
	float CannonAimRotationRate;

	/** Original UT3 projectile spread coefficient. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Goliath|Cannon")
	float CannonSpread;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Goliath|Cannon")
	UParticleSystem* CannonMuzzleEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Goliath|Cannon")
	USoundBase* CannonFireSound;

protected:
	virtual void OnPrimaryFirePressed() override;
	virtual void OnPrimaryFireReleased() override;
	virtual bool HandleDriverLeaveRequest() override;

	UFUNCTION(Reliable, Server, WithValidation)
	void ServerSetCannonFiring(bool bNewFiring, FRotator NewAimRotation);

	UFUNCTION(Unreliable, Server, WithValidation)
	void ServerSetCannonAim(FRotator NewAimRotation);

	UFUNCTION()
	void OnRep_CannonAim();

	UFUNCTION(Unreliable, NetMulticast)
	void MulticastPlayCannonFire(FVector MuzzleLocation, FRotator MuzzleRotation);

	void SetCannonFiring(bool bNewFiring);
	void FireCannon();
	void UpdateCannonAim(float DeltaSeconds);
	void ApplyTankDrive(float DeltaSeconds);
	void ApplyTankSteering();
	void UpdateVacantParking(float DeltaSeconds);
	bool FindParkingGround(float& OutGroundZ, float& OutParkedActorZ) const;
	float GetParkedHeightAboveGround() const;
	FRotator GetDesiredCannonAimRotation() const;
	FRotator GetCannonAimRotation() const;
	FRotator SanitizeCannonAim(const FRotator& AimRotation) const;

	UPROPERTY(ReplicatedUsing = OnRep_CannonAim)
	FRotator ReplicatedCannonAim;

	bool bCannonFiring;
	float NextCannonFireTime;
	float LastCannonAimSendTime;
	FRotator CurrentCannonAim;
	FRotator LastSentCannonAim;
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
