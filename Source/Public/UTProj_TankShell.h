#pragma once

#include "UTProjectile.h"
#include "UTProj_TankShell.generated.h"

/** Native UT3 Goliath shell: fast, short-lived, and strongly radial. */
UCLASS(Blueprintable)
class UTVEHICLES_API AUTProj_TankShell : public AUTProjectile
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = Projectile)
	class UStaticMeshComponent* ShellMesh;
};
