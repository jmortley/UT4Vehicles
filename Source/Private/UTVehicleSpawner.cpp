#include "UTVehicleSpawner.h"
#include "UnrealTournament.h"
#include "UTVehicle.h"
#include "UTVehicleFlying.h"
#include "UTVehicleComponent.h"

AUTVehicleSpawner::AUTVehicleSpawner(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PadMesh"));
	RootComponent = PadMesh;

	// Use a simple cylinder as spawn pad indicator
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Engine/BasicShapes/Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		PadMesh->SetStaticMesh(CylinderMesh.Object);
		PadMesh->SetWorldScale3D(FVector(3.0f, 3.0f, 0.1f));
	}

	PadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	VehicleClass = nullptr;
	TeamNum = 255;
	RespawnDelay = 15.0f;
	bSpawnOnBeginPlay = true;
	SpawnedVehicle = nullptr;

	SetReplicates(true);
	bAlwaysRelevant = true;
}

void AUTVehicleSpawner::BeginPlay()
{
	Super::BeginPlay();

	if (bSpawnOnBeginPlay && Role == ROLE_Authority)
	{
		SpawnVehicle();
	}
}

APawn* AUTVehicleSpawner::SpawnVehicle()
{
	if (Role != ROLE_Authority || VehicleClass == nullptr)
	{
		return nullptr;
	}

	// Don't spawn if one already exists
	if (SpawnedVehicle != nullptr && !SpawnedVehicle->IsPendingKillPending())
	{
		return SpawnedVehicle;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	FVector SpawnLocation = GetActorLocation() + FVector(0.0f, 0.0f, 200.0f);
	FRotator SpawnRotation = GetActorRotation();

	SpawnedVehicle = GetWorld()->SpawnActor<APawn>(VehicleClass, SpawnLocation, SpawnRotation, SpawnParams);

	if (SpawnedVehicle != nullptr)
	{
		// Set team on the vehicle component
		UUTVehicleComponent* VC = SpawnedVehicle->FindComponentByClass<UUTVehicleComponent>();
		if (VC != nullptr)
		{
			VC->TeamNum = TeamNum;
		}

		// Listen for destruction
		SpawnedVehicle->OnDestroyed.AddDynamic(this, &AUTVehicleSpawner::OnVehicleDestroyed);
	}

	return SpawnedVehicle;
}

void AUTVehicleSpawner::OnVehicleDestroyed(AActor* DestroyedActor)
{
	SpawnedVehicle = nullptr;

	if (Role == ROLE_Authority && RespawnDelay > 0.0f)
	{
		GetWorldTimerManager().SetTimer(RespawnTimerHandle, this, &AUTVehicleSpawner::RespawnTimerFired, RespawnDelay, false);
	}
}

void AUTVehicleSpawner::RespawnTimerFired()
{
	SpawnVehicle();
}
