#pragma once

#include "GameFramework/Actor.h"
#include "UTVehicleEntryProxy.generated.h"

class APlayerController;
class UWorld;

/**
 * Per-player replicated bridge for requesting entry into an unowned vehicle.
 *
 * Empty vehicles deliberately have no network owner.  This actor is spawned
 * by the server, owned by one player controller, and is therefore a legal RPC
 * target for that player's entry requests.  The server resolves the player's
 * current pawn and validates range/team/vacancy in UUTVehicleComponent.
 */
UCLASS(NotPlaceable, Transient)
class UTVEHICLES_API AUTVehicleEntryProxy : public AActor
{
	GENERATED_UCLASS_BODY()

public:
	/** Submit an entry request through this player's owned network channel. */
	UFUNCTION(Reliable, Server, WithValidation)
	void ServerTryEnterVehicle(AActor* VehicleActor);

	/** Find the proxy owned by Controller in the current world. */
	static AUTVehicleEntryProxy* FindForController(UWorld* World, APlayerController* Controller);
};
