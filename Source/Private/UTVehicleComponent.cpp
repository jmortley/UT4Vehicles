#include "UTVehicleComponent.h"
#include "UnrealTournament.h"
#include "UnrealNetwork.h"
#include "UTCharacter.h"
#include "UTPlayerController.h"
#include "UTPlayerState.h"
#include "UTGameState.h"
#include "UTHUD.h"

UUTVehicleComponent::UUTVehicleComponent()
{
	SetIsReplicated(true);

	Health = 600;
	HealthMax = 600;
	TeamNum = 255;
	bDead = false;
	EntryRadius = 300.0f;
	RespawnDelay = 15.0f;
	Driver = nullptr;
	DamageInstigator = nullptr;
	EntryTrigger = nullptr;
	PendingDriver = nullptr;
	SavedMaxSafeFallSpeed = 2400.0f;
}

void UUTVehicleComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UUTVehicleComponent, Driver);
	DOREPLIFETIME(UUTVehicleComponent, Health);
	DOREPLIFETIME(UUTVehicleComponent, HealthMax);
	DOREPLIFETIME(UUTVehicleComponent, TeamNum);
	DOREPLIFETIME(UUTVehicleComponent, bDead);
}

void UUTVehicleComponent::BeginPlay()
{
	Super::BeginPlay();
	InitEntryTrigger();
}

void UUTVehicleComponent::InitEntryTrigger()
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return;
	}

	EntryTrigger = NewObject<USphereComponent>(Owner, TEXT("VehicleEntryTrigger"));
	if (EntryTrigger != nullptr)
	{
		EntryTrigger->InitSphereRadius(EntryRadius);
		EntryTrigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
		EntryTrigger->bGenerateOverlapEvents = true;
		EntryTrigger->bTraceComplexOnMove = false;
		EntryTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
		EntryTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		EntryTrigger->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		EntryTrigger->RegisterComponent();

		EntryTrigger->OnComponentBeginOverlap.AddDynamic(this, &UUTVehicleComponent::OnEntryTriggerBeginOverlap);
		EntryTrigger->OnComponentEndOverlap.AddDynamic(this, &UUTVehicleComponent::OnEntryTriggerEndOverlap);
	}
}

void UUTVehicleComponent::OnEntryTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	APawn* OtherPawn = Cast<APawn>(OtherActor);
	if (OtherPawn == nullptr)
	{
		return;
	}

	OverlappingPawns.AddUnique(OtherPawn);

	// Option A: auto-enter on overlap if vehicle is empty.
	// Defer to next frame — entering during the physics overlap callback
	// causes damage/death because actor state changes mid-physics-step are unsafe.
	AActor* Owner = GetOwner();
	if (Owner != nullptr && Owner->Role == ROLE_Authority && Driver == nullptr)
	{
		PendingDriver = OtherPawn;
		GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UUTVehicleComponent::DeferredTryEnter);
	}
}

void UUTVehicleComponent::OnEntryTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	APawn* OtherPawn = Cast<APawn>(OtherActor);
	if (OtherPawn != nullptr)
	{
		OverlappingPawns.Remove(OtherPawn);
	}
}

void UUTVehicleComponent::DeferredTryEnter()
{
	if (PendingDriver != nullptr && Driver == nullptr && !bDead)
	{
		TryToDrive(PendingDriver);
	}
	PendingDriver = nullptr;
}

bool UUTVehicleComponent::IsInEntryRange(APawn* TestPawn) const
{
	return OverlappingPawns.Contains(TestPawn);
}

bool UUTVehicleComponent::TryToDrive(APawn* NewDriver)
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr || Owner->Role != ROLE_Authority)
	{
		return false;
	}
	if (bDead || NewDriver == nullptr || Driver != nullptr)
	{
		return false;
	}
	if (!IsInEntryRange(NewDriver))
	{
		return false;
	}

	// Team check
	AUTCharacter* UTChar = Cast<AUTCharacter>(NewDriver);
	if (UTChar != nullptr && TeamNum != 255)
	{
		AUTPlayerState* PS = Cast<AUTPlayerState>(UTChar->PlayerState);
		if (PS != nullptr && PS->GetTeamNum() != TeamNum)
		{
			return false;
		}
	}

	return DriverEnter(NewDriver);
}

bool UUTVehicleComponent::DriverEnter(APawn* NewDriver)
{
	AActor* Owner = GetOwner();
	APawn* VehiclePawn = Cast<APawn>(Owner);
	if (Owner == nullptr || Owner->Role != ROLE_Authority || VehiclePawn == nullptr)
	{
		return false;
	}
	if (Driver != nullptr)
	{
		DriverLeave(true);
	}
	if (NewDriver == nullptr)
	{
		return false;
	}

	Driver = NewDriver;
	AController* C = NewDriver->Controller;
	if (C != nullptr)
	{
		DamageInstigator = C;

		AUTCharacter* UTChar = Cast<AUTCharacter>(NewDriver);
		if (UTChar != nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Vehicle] DriverEnter: Health=%d IsDead=%d before entry"), UTChar->Health, UTChar->IsDead() ? 1 : 0);
		}

		// Redeemer pattern
		C->UnPossess();

		if (UTChar != nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Vehicle] After UnPossess: Health=%d IsDead=%d"), UTChar->Health, UTChar->IsDead() ? 1 : 0);
		}

		NewDriver->SetOwner(Owner);
		C->Possess(VehiclePawn);

		UE_LOG(LogTemp, Warning, TEXT("[Vehicle] After Possess: Vehicle.Controller=%s, Role=%d"), VehiclePawn->Controller ? *VehiclePawn->Controller->GetName() : TEXT("NONE"), (int32)VehiclePawn->Role);

		if (UTChar != nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Vehicle] After Possess vehicle: Health=%d IsDead=%d"), UTChar->Health, UTChar->IsDead() ? 1 : 0);
			UTChar->StartDriving(VehiclePawn);
			VehiclePawn->PlayerState = UTChar->PlayerState;
		}
	}

	return true;
}

bool UUTVehicleComponent::DriverLeave(bool bForceLeave)
{
	AActor* Owner = GetOwner();
	APawn* VehiclePawn = Cast<APawn>(Owner);
	if (Owner == nullptr || VehiclePawn == nullptr)
	{
		return false;
	}

	AController* C = VehiclePawn->Controller;
	if (Driver != nullptr && C != nullptr)
	{
		// Handle spectators viewing this vehicle
		AUTGameState* GS = GetWorld()->GetGameState<AUTGameState>();
		if (C->PlayerState != nullptr && GS != nullptr && !GS->IsMatchIntermission() && !GS->HasMatchEnded())
		{
			for (FLocalPlayerIterator It(GEngine, GetWorld()); It; ++It)
			{
				AUTPlayerController* UTPC = Cast<AUTPlayerController>(It->PlayerController);
				if (UTPC != nullptr && UTPC->LastSpectatedPlayerState == C->PlayerState)
				{
					UTPC->ViewPawn(Driver);
				}
			}
		}

		// Transfer possession back: vehicle -> character
		C->UnPossess();
		Driver->SetOwner(C);

		AUTCharacter* UTChar = Cast<AUTCharacter>(Driver);
		if (UTChar != nullptr)
		{
			UTChar->StopDriving(VehiclePawn);

			// Place driver behind the vehicle BEFORE re-enabling collision
			FVector ExitLocation = Owner->GetActorLocation() + Owner->GetActorForwardVector() * -200.0f + FVector(0.0f, 0.0f, 100.0f);
			UTChar->TeleportTo(ExitLocation, Owner->GetActorRotation(), false, true);

			// Restore fall damage threshold and re-enable movement
			UTChar->MaxSafeFallSpeed = SavedMaxSafeFallSpeed;
			if (UTChar->GetCharacterMovement() != nullptr)
			{
				UTChar->GetCharacterMovement()->Velocity = FVector::ZeroVector;
				UTChar->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			}
			UTChar->SetActorEnableCollision(true);
			UTChar->SetActorHiddenInGame(false);

			if (UTChar->GetWeapon() != nullptr)
			{
				UTChar->GetWeapon()->AddAmmo(0);
			}
		}

		C->Possess(Driver);
		VehiclePawn->PlayerState = nullptr;
	}

	Driver = nullptr;
	return true;
}

float UUTVehicleComponent::ApplyDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (bDead)
	{
		return 0.0f;
	}

	Health -= FMath::TruncToInt(Damage);
	if (Health <= 0)
	{
		Health = 0;
		VehicleDied(EventInstigator);
	}

	return Damage;
}

void UUTVehicleComponent::VehicleDied(AController* Killer)
{
	bDead = true;

	// Eject driver before destroying
	if (Driver != nullptr)
	{
		DriverLeave(true);
	}

	AActor* Owner = GetOwner();
	if (Owner != nullptr)
	{
		// TODO: Play explosion effects
		Owner->SetLifeSpan(3.0f);
	}
}

void UUTVehicleComponent::OnRep_Driver()
{
	// Client-side visual updates when driver enters/leaves
	if (Driver != nullptr)
	{
		AUTCharacter* UTChar = Cast<AUTCharacter>(Driver);
		if (UTChar != nullptr)
		{
			UTChar->SetActorHiddenInGame(true);
			UTChar->SetActorEnableCollision(false);
		}
	}
}

void UUTVehicleComponent::DrawVehicleHUD(AUTHUD* HUD, UCanvas* Canvas, APlayerController* ViewingPC)
{
	if (Canvas == nullptr || HUD == nullptr)
	{
		return;
	}

	// Draw health bar
	float ScreenWidth = Canvas->ClipX;
	float ScreenHeight = Canvas->ClipY;

	float BarWidth = 200.0f;
	float BarHeight = 16.0f;
	float BarX = (ScreenWidth - BarWidth) * 0.5f;
	float BarY = ScreenHeight - 80.0f;

	float HealthPct = (HealthMax > 0) ? FMath::Clamp((float)Health / (float)HealthMax, 0.0f, 1.0f) : 0.0f;

	// Background
	Canvas->SetDrawColor(0, 0, 0, 128);
	Canvas->DrawTile(Canvas->DefaultTexture, BarX, BarY, BarWidth, BarHeight, 0, 0, 1, 1);

	// Health fill
	FLinearColor HealthColor = FLinearColor::LerpUsingHSV(FLinearColor::Red, FLinearColor::Green, HealthPct);
	Canvas->SetDrawColor(HealthColor.ToFColor(true));
	Canvas->DrawTile(Canvas->DefaultTexture, BarX, BarY, BarWidth * HealthPct, BarHeight, 0, 0, 1, 1);

	// Health text
	Canvas->SetDrawColor(255, 255, 255, 255);
	FString HealthText = FString::Printf(TEXT("%d / %d"), Health, HealthMax);
	float TextWidth, TextHeight;
	Canvas->StrLen(GEngine->GetSmallFont(), HealthText, TextWidth, TextHeight);
	Canvas->DrawText(GEngine->GetSmallFont(), HealthText, BarX + (BarWidth - TextWidth) * 0.5f, BarY - TextHeight - 2.0f);
}
