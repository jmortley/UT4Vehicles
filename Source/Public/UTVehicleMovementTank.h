#pragma once

#include "WheeledVehicleMovementComponent4W.h"
#include "UTVehicleMovementTank.generated.h"

/**
 * Four-wheel PhysX drive with read access to the interpolated, replicated
 * inputs needed by a native skid-steer chassis. PhysX 4.15 has no exposed
 * tank drive component, so the Goliath supplies its differential yaw force
 * from the same authoritative input state used by the wheel simulation.
 */
UCLASS()
class UTVEHICLES_API UUTVehicleMovementTank : public UWheeledVehicleMovementComponent4W
{
	GENERATED_UCLASS_BODY()

public:
	float GetAppliedTankSteering() const { return SteeringInput; }
	float GetAppliedSignedThrottle() const
	{
		return (GetTargetGear() < 0 ? -ThrottleInput : ThrottleInput);
	}
};
