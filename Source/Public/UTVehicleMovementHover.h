#pragma once

#include "GameFramework/PawnMovementComponent.h"
#include "UTVehicleMovementHover.generated.h"

/**
 * Replicated state for hover/flight vehicles.
 * Mirrors FReplicatedVehicleState from WheeledVehicleMovementComponent.
 */
USTRUCT()
struct FReplicatedHoverState
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	float ThrottleInput;

	UPROPERTY()
	float SteeringInput;

	UPROPERTY()
	float LiftInput;

	UPROPERTY()
	float PitchInput;

	UPROPERTY()
	FRotator Rotation;

	FReplicatedHoverState()
		: ThrottleInput(0.0f)
		, SteeringInput(0.0f)
		, LiftInput(0.0f)
		, PitchInput(0.0f)
		, Rotation(FRotator::ZeroRotator)
	{}
};

/**
 * Custom movement component for hover/flight vehicles (Raptor, Manta, Fury, Cicada).
 * UT3-style flight: mouse aims/pitches, WASD for thrust, Space/Ctrl for altitude.
 * Handles its own replication via ServerUpdateState RPC.
 */
UCLASS(meta = (BlueprintSpawnableComponent))
class UTVEHICLES_API UUTVehicleMovementHover : public UPawnMovementComponent
{
	GENERATED_BODY()

public:
	UUTVehicleMovementHover();

	// --- Tuning properties ---

	/** Maximum forward speed (UU/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Movement)
	float MaxSpeed;

	/** Maximum altitude above ground (UU) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Movement)
	float MaxAltitude;

	/** Forward thrust force */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Movement)
	float ThrustForce;

	/** Vertical lift force */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Movement)
	float LiftForce;

	/** Strafe force (lateral movement) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Movement)
	float StrafeForce;

	/** Gravity applied when not actively lifting */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Movement)
	float GravityForce;

	/** Yaw turn rate in degrees/sec */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Movement)
	float TurnRate;

	/** Pitch rate multiplier for mouse look */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Movement)
	float PitchRate;

	/** Velocity damping factor (0-1, higher = more damping) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Movement, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VelocityDamping;

	/** Minimum hover height above ground (UU) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Movement)
	float MinHoverHeight;

	// --- Input ---

	/** Set throttle input (-1 to 1) */
	void SetThrottleInput(float Value);

	/** Set steering/yaw input (-1 to 1) */
	void SetSteeringInput(float Value);

	/** Set lift input (-1 to 1, positive = up) */
	void SetLiftInput(float Value);

	/** Set pitch input from mouse look */
	void SetPitchInput(float Value);

	// --- Replication ---

	UPROPERTY(Transient, Replicated)
	FReplicatedHoverState ReplicatedState;

	/** Send current input state to server */
	UFUNCTION(Reliable, Server, WithValidation)
	void ServerUpdateState(float InThrottle, float InSteering, float InLift, float InPitch);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// --- Movement ---

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Get the current speed */
	float GetCurrentSpeed() const;

protected:
	/** Raw input values (local) */
	float RawThrottleInput;
	float RawSteeringInput;
	float RawLiftInput;
	float RawPitchInput;

	/** Apply physics simulation for one tick */
	void UpdateFlightPhysics(float DeltaTime);

	/** Check ground distance for hover behavior */
	float GetGroundDistance() const;
};
