#pragma once

#include "CoreMinimal.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerInput.h"
#include "Curves/CurveLinearColor.h"
#include "Engine/Canvas.h"
#include "UTDamageType.h"
#include "UTVehicleDamageType.generated.h"

class UUTVehicleComponent;

/**
 * Damage type base for vehicle weapons and abilities owned by this plugin.
 *
 * UT4's stock damage types do not expose the vehicle scaling fields that UT3
 * had on DamageType. Plugin damage types derive from this class instead, while
 * UUTVehicleComponent supplies a compatibility table for stock UT4 weapons.
 */
UCLASS(Abstract, Blueprintable)
class UTVEHICLES_API UUTVehicleDamageType : public UUTDamageType
{
	GENERATED_UCLASS_BODY()

public:
	/** Multiplier applied when this damage type hits a vehicle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vehicle|Damage", meta = (ClampMin = "0.0"))
	float VehicleDamageScaling;

	/** UT3-style momentum multiplier for upcoming AVRIL and vehicle-weapon impulse handling. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vehicle|Damage", meta = (ClampMin = "0.0"))
	float VehicleMomentumScaling;

	/** Replace the incoming amount instead of multiplying it (Scorpion self-destruct uses 610). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vehicle|Damage")
	bool bUseFixedVehicleDamage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vehicle|Damage", meta = (EditCondition = "bUseFixedVehicleDamage", ClampMin = "0.0"))
	float FixedVehicleDamage;

	/** Override for vehicle-dependent rules such as the Goliath shell versus light armor. */
	virtual float GetVehicleDamageScalingFor(const UUTVehicleComponent* Vehicle) const;

	/** Override when a damage type needs more than a multiplier. */
	virtual float CalculateVehicleDamage(float IncomingDamage, const UUTVehicleComponent* Vehicle) const;
};

/** UT3 AVRiL: 1.6x vehicle damage and 5x vehicle momentum. */
UCLASS(Blueprintable)
class UTVEHICLES_API UUTDmgType_AvrilRocket : public UUTVehicleDamageType
{
	GENERATED_UCLASS_BODY()
};

/** UT3 Goliath shell: full damage normally and 1.2x against light armor. */
UCLASS(Blueprintable)
class UTVEHICLES_API UUTDmgType_TankShell : public UUTVehicleDamageType
{
	GENERATED_UCLASS_BODY()

public:
	virtual float GetVehicleDamageScalingFor(const UUTVehicleComponent* Vehicle) const override;
};

/** UT3 Scorpion self-destruct replaces any positive vehicle hit with exactly 610 damage. */
UCLASS(Blueprintable)
class UTVEHICLES_API UUTDmgType_ScorpionSelfDestruct : public UUTVehicleDamageType
{
	GENERATED_UCLASS_BODY()
};

/** UT3 Scorpion blade: armor-piercing, non-gibbing fatal contact damage. */
UCLASS(Blueprintable)
class UTVEHICLES_API UUTDmgType_ScorpionBlade : public UUTDamageType
{
	GENERATED_UCLASS_BODY()
};

/** UT3-style credited, non-gibbing roadkill damage. */
UCLASS(Blueprintable)
class UTVEHICLES_API UUTDmgType_RanOver : public UUTDamageType
{
	GENERATED_UCLASS_BODY()
};
