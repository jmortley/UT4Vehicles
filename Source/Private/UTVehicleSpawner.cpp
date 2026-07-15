#include "UTVehicleSpawner.h"
#include "UnrealTournament.h"
#include "UTVehicle.h"
#include "UTVehicleFlying.h"
#include "UTVehicleComponent.h"
#include "UnrealNetwork.h"

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
	PadMesh->SetHiddenInGame(true);

	VehicleClass = nullptr;
	TeamNum = 255;
	RespawnDelay = 15.0f;
	bSpawnOnBeginPlay = true;
	// Maps place the pad at the already-tested vehicle transform. Keep the
	// default exact; designers can add clearance per vehicle if a mesh needs it.
	SpawnOffset = FVector::ZeroVector;
	SpawnedVehicle = nullptr;

	SetReplicates(true);
	bAlwaysRelevant = true;
}

void AUTVehicleSpawner::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AUTVehicleSpawner, SpawnedVehicle);
}

void AUTVehicleSpawner::BeginPlay()
{
	Super::BeginPlay();

	if (bSpawnOnBeginPlay && Role == ROLE_Authority)
	{
		SpawnVehicle();
	}
}

void AUTVehicleSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(RespawnTimerHandle);
	if (SpawnedVehicle != nullptr)
	{
		SpawnedVehicle->OnDestroyed.RemoveDynamic(this, &AUTVehicleSpawner::OnVehicleDestroyed);
	}
	Super::EndPlay(EndPlayReason);
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
	GetWorldTimerManager().ClearTimer(RespawnTimerHandle);

	FActorSpawnParameters SpawnParams;
	// Keep empty vehicles off every player's RPC channel. The entry proxy owns
	// the request; DriverEnter assigns the vehicle to the winning controller.
	SpawnParams.Owner = nullptr;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	// The editor pad mesh is deliberately scaled, so apply rotation but never
	// inherit that visual scale into the gameplay spawn offset.
	FVector SpawnLocation = GetActorLocation() + GetActorRotation().RotateVector(SpawnOffset);
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
		ForceNetUpdate();
		UE_LOG(LogTemp, Warning, TEXT("[VehicleSpawner] Spawned Spawner=%s Vehicle=%s Class=%s Team=%d Location=%s"),
			*GetName(), *GetNameSafe(SpawnedVehicle), *GetNameSafe(*VehicleClass),
			(int32)TeamNum, *SpawnedVehicle->GetActorLocation().ToString());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[VehicleSpawner] Spawn failed Spawner=%s Class=%s"),
			*GetName(), *GetNameSafe(*VehicleClass));
	}

	return SpawnedVehicle;
}

void AUTVehicleSpawner::OnVehicleDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor != SpawnedVehicle)
	{
		return;
	}
	SpawnedVehicle = nullptr;
	ForceNetUpdate();

	if (Role == ROLE_Authority && RespawnDelay > 0.0f)
	{
		GetWorldTimerManager().SetTimer(RespawnTimerHandle, this, &AUTVehicleSpawner::RespawnTimerFired, RespawnDelay, false);
	}
}

void AUTVehicleSpawner::RespawnTimerFired()
{
	SpawnVehicle();
}
