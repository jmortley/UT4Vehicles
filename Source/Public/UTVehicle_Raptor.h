#pragma once

#include "UTVehicleFlying.h"
#include "UTVehicle_Raptor.generated.h"

/**
 * UT3 Raptor - Flying attack vehicle.
 * Uses SK_VH_Raptor skeletal mesh from UT3 assets.
 * UT3-style flight controls: mouse aim/pitch, WASD thrust, Space/Ctrl altitude.
 */
UCLASS()
class UTVEHICLES_API AUTVehicle_Raptor : public AUTVehicleFlying
{
	GENERATED_UCLASS_BODY()
};
