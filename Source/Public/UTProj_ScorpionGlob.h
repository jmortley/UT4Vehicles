#pragma once

#include "CoreMinimal.h"
#include "UTProjectile.h"
#include "UTProj_ScorpionGlob.generated.h"

/** Native UT3 Scorpion plasma glob. */
UCLASS(Blueprintable)
class UTVEHICLES_API AUTProj_ScorpionGlob : public AUTProjectile
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = Effects)
	class UParticleSystemComponent* FlightEffectComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Effects)
	class UParticleSystem* ImpactEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Effects)
	class USoundBase* ImpactSound;

	virtual void Explode_Implementation(const FVector& HitLocation, const FVector& HitNormal,
		UPrimitiveComponent* HitComp = nullptr) override;
};
