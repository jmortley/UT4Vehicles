#include "UTVehicleEntryProxy.h"
#include "UTVehicleComponent.h"
#include "UnrealTournament.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

AUTVehicleEntryProxy::AUTVehicleEntryProxy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(true);
	SetReplicateMovement(false);
	bOnlyRelevantToOwner = true;
	bAlwaysRelevant = false;
	bNetLoadOnClient = false;
	NetUpdateFrequency = 10.0f;
}

AUTVehicleEntryProxy* AUTVehicleEntryProxy::FindForController(UWorld* World, APlayerController* Controller)
{
	if (World == nullptr || Controller == nullptr)
	{
		return nullptr;
	}

	for (TActorIterator<AUTVehicleEntryProxy> It(World); It; ++It)
	{
		if (It->GetOwner() == Controller)
		{
			return *It;
		}
	}
	return nullptr;
}

bool AUTVehicleEntryProxy::ServerTryEnterVehicle_Validate(AActor* VehicleActor)
{
	// Implementation performs all gameplay validation. Returning true here
	// avoids disconnecting a client if its target is destroyed in transit.
	return true;
}

void AUTVehicleEntryProxy::ServerTryEnterVehicle_Implementation(AActor* VehicleActor)
{
	APlayerController* RequestingPC = Cast<APlayerController>(GetOwner());
	APawn* RequestingPawn = RequestingPC != nullptr ? RequestingPC->GetPawn() : nullptr;
	UUTVehicleComponent* VehicleComponent = VehicleActor != nullptr
		? VehicleActor->FindComponentByClass<UUTVehicleComponent>()
		: nullptr;

	UE_LOG(LogTemp, Warning, TEXT("[VehicleEntry] Server request Proxy=%s PC=%s Pawn=%s Vehicle=%s Component=%s"),
		*GetName(), *GetNameSafe(RequestingPC), *GetNameSafe(RequestingPawn),
		*GetNameSafe(VehicleActor), *GetNameSafe(VehicleComponent));

	if (RequestingPC != nullptr && RequestingPawn != nullptr && VehicleComponent != nullptr)
	{
		// Competing requests are serialized by the server. The first request to
		// pass TryToDrive wins; later requests see the occupied Driver field.
		VehicleComponent->TryToDrive(RequestingPawn);
	}
}
