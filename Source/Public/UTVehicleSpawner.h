#pragma once

#include "UTVehicleSpawner.generated.h"

/**
 * Place in maps to spawn a vehicle. Respawns after the vehicle is destroyed.
 * Supports team restriction so red/blue teams get their own spawn pads.
 */
UCLASS(Blueprintable)
class UTVEHICLES_API AUTVehicleSpawner : public AActor
{
	GENERATED_UCLASS_BODY()

public:
	/** Vehicle class to spawn */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Vehicle)
	TSubclassOf<APawn> VehicleClass;

	/** Team that owns this spawner (255 = any team) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Vehicle)
	uint8 TeamNum;

	/** Delay before respawning after vehicle is destroyed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Vehicle)
	float RespawnDelay;

	/** Should spawn a vehicle on BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Vehicle)
	bool bSpawnOnBeginPlay;

	/** The currently spawned vehicle */
	UPROPERTY(BlueprintReadOnly, Category = Vehicle)
	APawn* SpawnedVehicle;

	virtual void BeginPlay() override;

	/** Spawn a new vehicle at this location */
	UFUNCTION(BlueprintCallable, Category = Vehicle)
	APawn* SpawnVehicle();

	/** Called when our vehicle is destroyed */
	UFUNCTION()
	void OnVehicleDestroyed(AActor* DestroyedActor);

	/** Timer callback to respawn */
	void RespawnTimerFired();

protected:
	FTimerHandle RespawnTimerHandle;

	/** Visual indicator mesh */
	UPROPERTY(VisibleAnywhere, Category = Vehicle)
	UStaticMeshComponent* PadMesh;
};
