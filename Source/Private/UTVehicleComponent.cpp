#include "UTVehicleComponent.h"
#include "UTVehicle.h"
#include "UTVehicleFlying.h"
#include "UnrealTournament.h"
#include "UnrealNetwork.h"
#include "Components/InputComponent.h"
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
	EntryInputComponent = nullptr;
	EntryInputPC = nullptr;
	EntryInputPawn = nullptr;
	VisualDriver = nullptr;
	SavedMaxSafeFallSpeed = 2400.0f;
}

void UUTVehicleComponent::SetDriverVisualState(APawn* DriverPawn, bool bDriving)
{
	AUTCharacter* UTChar = Cast<AUTCharacter>(DriverPawn);
	if (UTChar == nullptr)
	{
		return;
	}

	UTChar->SetActorHiddenInGame(bDriving);
	UTChar->SetActorEnableCollision(!bDriving);
	if (UTChar->GetCharacterMovement() != nullptr)
	{
		if (bDriving)
		{
			UTChar->GetCharacterMovement()->StopMovementImmediately();
			UTChar->GetCharacterMovement()->DisableMovement();
		}
		else
		{
			UTChar->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
	}
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

void UUTVehicleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindEntryInput();
	Super::EndPlay(EndPlayReason);
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

	AActor* Owner = GetOwner();
	if (Owner != nullptr && Owner->Role == ROLE_Authority && Driver == nullptr)
	{
		// Server RPCs on an empty world vehicle are legal only after it is owned by
		// the overlapping player's connection. TryToDrive still validates range,
		// team and vacancy on the server when ActivateSpecial is pressed.
		RefreshEntryOwner();
	}

	if (OtherPawn->IsLocallyControlled())
	{
		BindEntryInput(OtherPawn);
	}
}

void UUTVehicleComponent::OnEntryTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	APawn* OtherPawn = Cast<APawn>(OtherActor);
	if (OtherPawn != nullptr)
	{
		OverlappingPawns.Remove(OtherPawn);
		if (OtherPawn == EntryInputPawn)
		{
			UnbindEntryInput();
		}
		if (GetOwner() != nullptr && GetOwner()->Role == ROLE_Authority && Driver == nullptr)
		{
			RefreshEntryOwner();
		}
	}
}

bool UUTVehicleComponent::IsInEntryRange(APawn* TestPawn) const
{
	return OverlappingPawns.Contains(TestPawn);
}

void UUTVehicleComponent::BindEntryInput(APawn* LocalPawn)
{
	APlayerController* PC = LocalPawn != nullptr ? Cast<APlayerController>(LocalPawn->Controller) : nullptr;
	if (PC == nullptr || !PC->IsLocalController())
	{
		return;
	}
	if (EntryInputPC == PC && EntryInputPawn == LocalPawn && EntryInputComponent != nullptr)
	{
		return;
	}

	UnbindEntryInput();
	EntryInputPC = PC;
	EntryInputPawn = LocalPawn;

	EntryInputComponent = NewObject<UInputComponent>(PC);
	if (EntryInputComponent != nullptr)
	{
		EntryInputComponent->Priority = 90;
		EntryInputComponent->bBlockInput = false;
		FInputActionBinding& EntryBinding = EntryInputComponent->BindAction(
			TEXT("ActivateSpecial"), IE_Pressed, this, &UUTVehicleComponent::OnActivateSpecialPressed);
		EntryBinding.bConsumeInput = true;
		EntryInputComponent->RegisterComponent();
		PC->PushInputComponent(EntryInputComponent);
	}

	if (PC->MyHUD != nullptr && GetOwner() != nullptr)
	{
		PC->MyHUD->AddPostRenderedActor(GetOwner());
	}
}

void UUTVehicleComponent::UnbindEntryInput()
{
	if (EntryInputPC != nullptr && GetOwner() != nullptr && EntryInputPC->MyHUD != nullptr)
	{
		EntryInputPC->MyHUD->RemovePostRenderedActor(GetOwner());
	}
	if (EntryInputPC != nullptr && EntryInputComponent != nullptr)
	{
		EntryInputPC->PopInputComponent(EntryInputComponent);
	}
	if (EntryInputComponent != nullptr)
	{
		EntryInputComponent->DestroyComponent();
	}

	EntryInputComponent = nullptr;
	EntryInputPC = nullptr;
	EntryInputPawn = nullptr;
}

void UUTVehicleComponent::OnActivateSpecialPressed()
{
	APawn* Candidate = EntryInputPawn;
	if (Candidate == nullptr && EntryInputPC != nullptr)
	{
		Candidate = EntryInputPC->GetPawn();
	}
	if (Candidate == nullptr || Driver != nullptr || bDead || !IsInEntryRange(Candidate))
	{
		return;
	}

	AActor* VehicleActor = GetOwner();
	if (VehicleActor == nullptr)
	{
		return;
	}
	if (VehicleActor->Role == ROLE_Authority)
	{
		TryToDrive(Candidate);
	}
	else if (AUTVehicle* WheeledVehicle = Cast<AUTVehicle>(VehicleActor))
	{
		WheeledVehicle->ServerTryEnter(Candidate);
	}
	else if (AUTVehicleFlying* FlyingVehicle = Cast<AUTVehicleFlying>(VehicleActor))
	{
		FlyingVehicle->ServerTryEnter(Candidate);
	}
}

void UUTVehicleComponent::RefreshEntryOwner()
{
	AActor* Vehicle = GetOwner();
	if (Vehicle == nullptr || Vehicle->Role != ROLE_Authority)
	{
		return;
	}

	AController* DesiredOwner = Driver != nullptr ? Driver->Controller : nullptr;
	if (DesiredOwner == nullptr)
	{
		for (APawn* Candidate : OverlappingPawns)
		{
			if (Candidate != nullptr && Candidate->Controller != nullptr)
			{
				DesiredOwner = Candidate->Controller;
				break;
			}
		}
	}
	if (Vehicle->GetOwner() != DesiredOwner)
	{
		Vehicle->SetOwner(DesiredOwner);
	}
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
		Owner->SetOwner(C);

		AUTCharacter* UTChar = Cast<AUTCharacter>(NewDriver);
		if (UTChar != nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Vehicle] DriverEnter: Health=%d IsDead=%d before entry"), UTChar->Health, UTChar->IsDead() ? 1 : 0);
			SavedMaxSafeFallSpeed = UTChar->MaxSafeFallSpeed;
		}

		// OnRep_Driver does not execute for the authority copy. Apply this
		// immediately so a listen host's old character does not remain visible,
		// collide with the vehicle, or get pushed into the air.
		SetDriverVisualState(NewDriver, true);
		VisualDriver = NewDriver;

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

		AUTVehicle* Vehicle = Cast<AUTVehicle>(VehiclePawn);
		if (Vehicle != nullptr)
		{
			Vehicle->PlayEnterSound();
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
		AUTVehicle* Vehicle = Cast<AUTVehicle>(VehiclePawn);
		if (Vehicle != nullptr)
		{
			Vehicle->PlayExitSound();
		}

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
			SetDriverVisualState(UTChar, false);

			if (UTChar->GetWeapon() != nullptr)
			{
				UTChar->GetWeapon()->AddAmmo(0);
			}
		}

		C->Possess(Driver);
		VehiclePawn->PlayerState = nullptr;
	}

	VisualDriver = nullptr;
	Driver = nullptr;
	RefreshEntryOwner();
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
	// Restore the previous client-side driver before applying a newly
	// replicated one. This also fixes leaving a vehicle, where Driver is null.
	if (VisualDriver != nullptr && VisualDriver != Driver)
	{
		SetDriverVisualState(VisualDriver, false);
		VisualDriver = nullptr;
	}

	if (Driver != nullptr)
	{
		SetDriverVisualState(Driver, true);
		VisualDriver = Driver;
		AUTVehicle* Vehicle = Cast<AUTVehicle>(GetOwner());
		if (Vehicle != nullptr)
		{
			Vehicle->PlayEnterSound();
		}
	}
	else
	{
		AUTVehicle* Vehicle = Cast<AUTVehicle>(GetOwner());
		if (Vehicle != nullptr)
		{
			Vehicle->PlayExitSound();
		}
	}
}

void UUTVehicleComponent::DrawEntryPrompt(AUTHUD* HUD, UCanvas* Canvas, APlayerController* ViewingPC)
{
	if (Canvas == nullptr || HUD == nullptr || ViewingPC == nullptr || Driver != nullptr || bDead)
	{
		return;
	}

	APawn* ViewingPawn = ViewingPC->GetPawn();
	if (ViewingPawn == nullptr || !IsInEntryRange(ViewingPawn))
	{
		return;
	}

	FString KeyName(TEXT("<none>"));
	AUTPlayerController* UTPC = Cast<AUTPlayerController>(ViewingPC);
	if (UTPC != nullptr)
	{
		TArray<FString> Keys;
		UTPC->ResolveKeybind(TEXT("ActivateSpecial"), Keys);
		if (Keys.Num() > 0)
		{
			KeyName = Keys[0];
		}
	}

	const FString Prompt = FString::Printf(TEXT("Press %s to enter vehicle"), *KeyName);
	UFont* PromptFont = GEngine != nullptr ? GEngine->GetMediumFont() : nullptr;
	if (PromptFont == nullptr)
	{
		return;
	}

	float TextWidth = 0.0f;
	float TextHeight = 0.0f;
	Canvas->TextSize(PromptFont, Prompt, TextWidth, TextHeight);
	const float PadX = 18.0f;
	const float PadY = 10.0f;
	const float TextX = (Canvas->ClipX - TextWidth) * 0.5f;
	const float TextY = Canvas->ClipY * 0.72f;

	Canvas->SetDrawColor(0, 0, 0, 180);
	Canvas->DrawTile(Canvas->DefaultTexture, TextX - PadX, TextY - PadY,
		TextWidth + PadX * 2.0f, TextHeight + PadY * 2.0f, 0, 0, 1, 1);
	Canvas->SetDrawColor(255, 220, 96, 255);
	Canvas->DrawText(PromptFont, Prompt, TextX, TextY);
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
