#include "UTVehicleDamageType.h"
#include "UnrealTournament.h"
#include "UTVehicleComponent.h"

UUTVehicleDamageType::UUTVehicleDamageType(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	VehicleDamageScaling = 1.0f;
	VehicleMomentumScaling = 1.0f;
	bUseFixedVehicleDamage = false;
	FixedVehicleDamage = 0.0f;
}

float UUTVehicleDamageType::GetVehicleDamageScalingFor(const UUTVehicleComponent*) const
{
	return VehicleDamageScaling;
}

float UUTVehicleDamageType::CalculateVehicleDamage(float IncomingDamage, const UUTVehicleComponent* Vehicle) const
{
	if (bUseFixedVehicleDamage)
	{
		return FMath::IsFinite(FixedVehicleDamage) ? FMath::Max(0.0f, FixedVehicleDamage) : 0.0f;
	}

	const float ScaledDamage = IncomingDamage * GetVehicleDamageScalingFor(Vehicle);
	return FMath::IsFinite(ScaledDamage) ? FMath::Max(0.0f, ScaledDamage) : 0.0f;
}

UUTDmgType_AvrilRocket::UUTDmgType_AvrilRocket(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	VehicleDamageScaling = 1.6f;
	VehicleMomentumScaling = 5.0f;
}

UUTDmgType_TankShell::UUTDmgType_TankShell(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	VehicleDamageScaling = 1.0f;
	VehicleMomentumScaling = 1.5f;
}

float UUTDmgType_TankShell::GetVehicleDamageScalingFor(const UUTVehicleComponent* Vehicle) const
{
	return Vehicle != nullptr && Vehicle->bLightArmor ? 1.2f : VehicleDamageScaling;
}

UUTDmgType_ScorpionSelfDestruct::UUTDmgType_ScorpionSelfDestruct(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bUseFixedVehicleDamage = true;
	FixedVehicleDamage = 610.0f;
}

UUTDmgType_ScorpionBlade::UUTDmgType_ScorpionBlade(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bBlockedByArmor = false;
	GibHealthThreshold = MIN_int32;
	GibDamageThreshold = MAX_int32;
}

UUTDmgType_RanOver::UUTDmgType_RanOver(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bBlockedByArmor = false;
	bCausesBlood = true;
	GibHealthThreshold = MIN_int32;
	GibDamageThreshold = MAX_int32;
	ConsoleDeathMessage = NSLOCTEXT("UTDeathMessages", "DeathMessage_RanOver",
		"{Player1Name} ran over {Player2Name}.");
	MaleSuicideMessage = NSLOCTEXT("UTDeathMessages", "MaleSuicideMessage_RanOver",
		"{Player2Name} ran himself over.");
	FemaleSuicideMessage = NSLOCTEXT("UTDeathMessages", "FemaleSuicideMessage_RanOver",
		"{Player2Name} ran herself over.");
	SelfVictimMessage = NSLOCTEXT("UTDeathMessages", "SelfVictimMessage_RanOver",
		"You were run over.");
}
