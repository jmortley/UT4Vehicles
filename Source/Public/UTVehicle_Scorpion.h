#pragma once

#include "UTVehicle.h"
#include "VehicleWheel.h"
#include "UTVehicle_Scorpion.generated.h"

/**
 * UT3 Scorpion - 4-wheeled ground vehicle with rear-wheel drive.
 * Uses SK_VH_Scorpion_001 skeletal mesh from UT3 assets.
 */
UCLASS()
class UTVEHICLES_API AUTVehicle_Scorpion : public AUTVehicle
{
	GENERATED_UCLASS_BODY()
};

/** Front wheel for Scorpion - steerable, no handbrake */
UCLASS()
class UTVEHICLES_API UUTWheel_Scorpion_Front : public UVehicleWheel
{
	GENERATED_BODY()

public:
	UUTWheel_Scorpion_Front();
};

/** Rear wheel for Scorpion - driven, handbrake affected */
UCLASS()
class UTVEHICLES_API UUTWheel_Scorpion_Rear : public UVehicleWheel
{
	GENERATED_BODY()

public:
	UUTWheel_Scorpion_Rear();
};
