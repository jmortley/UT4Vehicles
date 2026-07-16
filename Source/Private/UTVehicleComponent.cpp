#include "UTVehicleComponent.h"
#include "UTVehicle.h"
#include "UTVehicleDamageType.h"
#include "UTVehicleEntryProxy.h"
#include "UTVehicle_Scorpion.h"
#include "UnrealTournament.h"
#include "UnrealNetwork.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UTCharacter.h"
#include "UTWeapon.h"
#include "UTPlayerController.h"
#include "UTPlayerState.h"
#include "UTGameState.h"
#include "UTHUD.h"

namespace
{
	struct FUT3VehicleDamageRule
	{
		FUT3VehicleDamageRule(const TCHAR* InClassName, float InScaling, bool bInSniperPrimary = false)
			: ClassName(InClassName)
			, Scaling(InScaling)
			, bSniperPrimary(bInSniperPrimary)
		{
		}

		FName ClassName;
		float Scaling;
		bool bSniperPrimary;
	};

	float GetStockVehicleDamageScaling(UClass* DamageTypeClass, bool bLightArmor, float DefaultScaling)
	{
		// UT4 keeps these as unrelated Blueprint-generated UUTDamageType classes,
		// so native inheritance cannot recover the per-weapon values from UT3.
		// Walk parents as well so a Blueprint child inherits its parent's rule.
		static const FUT3VehicleDamageRule Rules[] =
		{
			FUT3VehicleDamageRule(TEXT("UTDmg_ImpactHammer_C"), 0.2f),
			FUT3VehicleDamageRule(TEXT("UTDmg_ImpactHammerBlock_C"), 1.0f),
			FUT3VehicleDamageRule(TEXT("UTDMG_Enforcer_C"), 0.33f),
			FUT3VehicleDamageRule(TEXT("UTDmg_BioGoo_C"), 0.8f),
			FUT3VehicleDamageRule(TEXT("UTDmg_BioGoo_Charged_C"), 0.8f),
			FUT3VehicleDamageRule(TEXT("UTDmg_BioCloud_C"), 0.8f),
			FUT3VehicleDamageRule(TEXT("UTDmg_ShockBeam_C"), 0.7f),
			FUT3VehicleDamageRule(TEXT("UTDmg_ShockCore_C"), 0.8f),
			FUT3VehicleDamageRule(TEXT("UTDmg_ShockCombo_C"), 0.8f),
			FUT3VehicleDamageRule(TEXT("UTDMG_Link_Primary_C"), 0.6f),
			FUT3VehicleDamageRule(TEXT("UTDMG_Link_Alt_C"), 0.8f),
			FUT3VehicleDamageRule(TEXT("UTDMG_Link_AltPulse_C"), 0.8f),
			FUT3VehicleDamageRule(TEXT("UTDMG_Minigun_Primary_C"), 0.6f),
			FUT3VehicleDamageRule(TEXT("UTDMG_Minigun_Alt_C"), 0.6f),
			FUT3VehicleDamageRule(TEXT("UTDMG_Minigun_Explosive_C"), 0.6f),
			FUT3VehicleDamageRule(TEXT("UTDmg_FlakShard_C"), 0.8f),
			FUT3VehicleDamageRule(TEXT("UTDmg_FlakShell_C"), 0.8f),
			FUT3VehicleDamageRule(TEXT("UTDMG_Rocket_C"), 0.8f),
			FUT3VehicleDamageRule(TEXT("UTDmg_Sniper_C"), 0.4f, true),
			FUT3VehicleDamageRule(TEXT("UTDmg_150cal_C"), 0.4f, true),
			FUT3VehicleDamageRule(TEXT("BP_UTDmg_SniperHeadshot_C"), 0.4f),
			FUT3VehicleDamageRule(TEXT("UTDmg_SniperHeadshot"), 0.4f),
			FUT3VehicleDamageRule(TEXT("UTDmg_Redeemer_C"), 1.5f),
			FUT3VehicleDamageRule(TEXT("UTDmg_InstagibBeam_C"), 1.0f),

			// Active UT4-only weapons use the nearest UT3 infantry role.
			FUT3VehicleDamageRule(TEXT("UTDmg_GrenadeLauncher_C"), 0.8f),
			FUT3VehicleDamageRule(TEXT("UTDmg_GrenadeLauncher_Sticky_C"), 0.8f),
			FUT3VehicleDamageRule(TEXT("UTDmg_GrenadeLauncher_Big_C"), 0.8f),
			FUT3VehicleDamageRule(TEXT("UTDmg_GrenadeLauncher_Small_C"), 0.8f),
			FUT3VehicleDamageRule(TEXT("UTDMG_LightningBeam_C"), 0.4f, true),
			FUT3VehicleDamageRule(TEXT("UTDMG_LightningProj_C"), 0.4f, true),
			FUT3VehicleDamageRule(TEXT("UTDMG_LightningBeamChain_C"), 0.4f, true)
		};

		for (UClass* TestClass = DamageTypeClass; TestClass != nullptr; TestClass = TestClass->GetSuperClass())
		{
			for (const FUT3VehicleDamageRule& Rule : Rules)
			{
				if (TestClass->GetFName() == Rule.ClassName)
				{
					return Rule.bSniperPrimary && bLightArmor ? Rule.Scaling * 1.5f : Rule.Scaling;
				}
			}
		}

		return DefaultScaling;
	}

	FString ResolveVehicleUseKey(APlayerController* PlayerController)
	{
		FString KeyName(TEXT("<none>"));
		AUTPlayerController* UTPC = Cast<AUTPlayerController>(PlayerController);
		if (UTPC != nullptr)
		{
			TArray<FString> Keys;
			UTPC->ResolveKeybind(TEXT("ActivateSpecial"), Keys);
			if (Keys.Num() > 0)
			{
				KeyName = Keys[0];
			}
		}
		return KeyName;
	}
}

UUTVehicleComponent::UUTVehicleComponent()
{
	SetIsReplicated(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	Health = 600;
	HealthMax = 600;
	bLightArmor = false;
	DefaultVehicleDamageScaling = 1.0f;
	TeamNum = 255;
	bDead = false;
	bEntryLocked = false;
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
	HornAttenuation = nullptr;
	LastHornTime = -1000.0f;

	// Use a stock sound that ships with this UT4 tree. Vehicle-specific horn
	// banks can override the component default once their assets are imported.
	static ConstructorHelpers::FObjectFinder<USoundBase> StockHornFinder(
		TEXT("/Game/RestrictedAssets/Audio/Accessories/TrainHorn03"));
	HornSound = StockHornFinder.Object;
}

void UUTVehicleComponent::SetDriverVisualState(APawn* DriverPawn, bool bDriving)
{
	AUTCharacter* UTChar = Cast<AUTCharacter>(DriverPawn);
	if (UTChar == nullptr)
	{
		return;
	}

	// UT's helper also hides the third-person weapon attachment, hat, and
	// eyewear. SetActorHiddenInGame() alone leaves the detached weapon visual at
	// the old character location, which looks like the player dropped the gun.
	UTChar->HideCharacter(bDriving);
	if (AUTWeapon* HeldWeapon = UTChar->GetWeapon())
	{
		HeldWeapon->SetActorHiddenInGame(bDriving);
	}
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
	DOREPLIFETIME(UUTVehicleComponent, bEntryLocked);
}

void UUTVehicleComponent::BeginPlay()
{
	Super::BeginPlay();

	HornAttenuation = NewObject<USoundAttenuation>(this, TEXT("VehicleHornAttenuationRuntime"));
	if (HornAttenuation != nullptr)
	{
		FAttenuationSettings& Settings = HornAttenuation->Attenuation;
		Settings.bAttenuate = true;
		Settings.bSpatialize = true;
		Settings.DistanceAlgorithm = ATTENUATION_Linear;
		Settings.AttenuationShape = EAttenuationShape::Sphere;
		Settings.AttenuationShapeExtents = FVector(600.0f, 0.0f, 0.0f);
		Settings.FalloffDistance = 6000.0f;
		Settings.OmniRadius = 150.0f;
	}
	InitEntryTrigger();
}

void UUTVehicleComponent::RequestHorn()
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return;
	}

	if (Owner->Role == ROLE_Authority)
	{
		PlayHornAuthoritative();
	}
	else
	{
		ServerRequestHorn();
	}
}

bool UUTVehicleComponent::ServerRequestHorn_Validate()
{
	return GetOwner() != nullptr && Driver != nullptr && !bDead;
}

void UUTVehicleComponent::ServerRequestHorn_Implementation()
{
	PlayHornAuthoritative();
}

void UUTVehicleComponent::PlayHornAuthoritative()
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (Owner == nullptr || World == nullptr || Driver == nullptr || bDead)
	{
		return;
	}

	// Cap repeat rate so a held/macroed key cannot flood a match with reliable
	// multicast traffic. UT3 horns are short one-shots, not looping components.
	const float Now = World->GetTimeSeconds();
	if (Now - LastHornTime < 0.25f)
	{
		return;
	}
	LastHornTime = Now;
	MulticastPlayHorn();
}

void UUTVehicleComponent::MulticastPlayHorn_Implementation()
{
	AActor* Owner = GetOwner();
	if (Owner != nullptr && HornSound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(Owner, HornSound, Owner->GetActorLocation(),
			1.0f, 1.0f, 0.0f, HornAttenuation);
	}
}

void UUTVehicleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindEntryInput();
	Super::EndPlay(EndPlayReason);
}

void UUTVehicleComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (EntryInputComponent != nullptr &&
		(EntryInputPC == nullptr || EntryInputPawn == nullptr ||
		 EntryInputPC->GetPawn() != EntryInputPawn || !CanEnterVehicle(EntryInputPawn)))
	{
		// Possession can change without producing an overlap-end callback for the
		// now-hidden character. Remove stale entry captures before they can consume
		// ActivateSpecial and block the possessed vehicle's exit binding.
		UnbindEntryInput();
	}
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
	if (Owner != nullptr && Owner->Role == ROLE_Authority)
	{
		// Empty vehicles remain unowned. Give each overlapping player one owned,
		// replicated RPC bridge instead of racing vehicle ownership between them.
		EnsureEntryProxy(OtherPawn->Controller);
	}

	if (OtherPawn->IsLocallyControlled() && CanEnterVehicle(OtherPawn))
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
	}
}

bool UUTVehicleComponent::IsInEntryRange(APawn* TestPawn) const
{
	return OverlappingPawns.Contains(TestPawn);
}

void UUTVehicleComponent::ClearLocalEntryInput(APlayerController* LocalPC)
{
	if (LocalPC != nullptr && EntryInputPC == LocalPC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[VehicleEntry] Clearing stale capture PC=%s Pawn=%s Component=%s Vehicle=%s"),
			*GetNameSafe(LocalPC), *GetNameSafe(EntryInputPawn),
			*GetNameSafe(EntryInputComponent), *GetNameSafe(GetOwner()));
		UnbindEntryInput();
	}
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
		// Stock UT binds ActivateSpecial on the player controller and other game
		// features may push their own capture components. Vehicle use must get the
		// first look at the key while the local pawn is inside the entry trigger.
		EntryInputComponent->Priority = 10000;
		EntryInputComponent->bBlockInput = false;
		FInputActionBinding& EntryBinding = EntryInputComponent->BindAction(
			TEXT("ActivateSpecial"), IE_Pressed, this, &UUTVehicleComponent::OnActivateSpecialPressed);
		EntryBinding.bConsumeInput = true;
		EntryInputComponent->RegisterComponent();
		PC->PushInputComponent(EntryInputComponent);
		UE_LOG(LogTemp, Warning, TEXT("[VehicleEntry] ActivateSpecial bound PC=%s Pawn=%s Priority=%d"),
			*GetNameSafe(PC), *GetNameSafe(LocalPawn), EntryInputComponent->Priority);
		SetComponentTickEnabled(true);
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
		// Possession can invalidate the nearby-entry capture after the vehicle has
		// already registered itself as the occupied HUD actor. Do not let cleanup of
		// the stale entry binding erase the driver's health/exit/eject HUD again.
		if (EntryInputPC->GetPawn() != GetOwner())
		{
			EntryInputPC->MyHUD->RemovePostRenderedActor(GetOwner());
		}
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
	SetComponentTickEnabled(false);
}

void UUTVehicleComponent::OnActivateSpecialPressed()
{
	APawn* Candidate = EntryInputPawn;
	if (Candidate == nullptr && EntryInputPC != nullptr)
	{
		Candidate = EntryInputPC->GetPawn();
	}
	const bool bCandidateInRange = Candidate != nullptr && IsInEntryRange(Candidate);
	const bool bCanEnter = CanEnterVehicle(Candidate);
	UE_LOG(LogTemp, Warning, TEXT("[VehicleEntry] ActivateSpecial pressed Vehicle=%s Owner=%s Candidate=%s InRange=%d CanEnter=%d Driver=%s Dead=%d Locked=%d Role=%d"),
		*GetNameSafe(GetOwner()), *GetNameSafe(GetOwner() != nullptr ? GetOwner()->GetOwner() : nullptr),
		*GetNameSafe(Candidate), bCandidateInRange ? 1 : 0, bCanEnter ? 1 : 0,
		*GetNameSafe(Driver), bDead ? 1 : 0, bEntryLocked ? 1 : 0,
		GetOwner() != nullptr ? (int32)GetOwner()->Role : -1);
	if (!bCanEnter)
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
	else
	{
		AUTVehicleEntryProxy* EntryProxy = AUTVehicleEntryProxy::FindForController(GetWorld(), EntryInputPC);
		if (EntryProxy != nullptr)
		{
			EntryProxy->ServerTryEnterVehicle(VehicleActor);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[VehicleEntry] No owned entry proxy yet for PC=%s"),
				*GetNameSafe(EntryInputPC));
		}
	}
}

void UUTVehicleComponent::EnsureEntryProxy(AController* Controller)
{
	AActor* Vehicle = GetOwner();
	APlayerController* PC = Cast<APlayerController>(Controller);
	UWorld* World = GetWorld();
	if (Vehicle == nullptr || Vehicle->Role != ROLE_Authority || PC == nullptr || World == nullptr)
	{
		return;
	}

	if (AUTVehicleEntryProxy::FindForController(World, PC) == nullptr)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = PC;
		SpawnParameters.Instigator = PC->GetPawn();
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AUTVehicleEntryProxy* Proxy = World->SpawnActor<AUTVehicleEntryProxy>(
			AUTVehicleEntryProxy::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
		UE_LOG(LogTemp, Warning, TEXT("[VehicleEntry] Entry proxy spawned PC=%s Proxy=%s"),
			*GetNameSafe(PC), *GetNameSafe(Proxy));
	}
}

bool UUTVehicleComponent::TryToDrive(APawn* NewDriver)
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr || Owner->Role != ROLE_Authority)
	{
		return false;
	}
	if (!CanEnterVehicle(NewDriver))
	{
		return false;
	}

	return DriverEnter(NewDriver);
}

bool UUTVehicleComponent::CanEnterVehicle(APawn* NewDriver) const
{
	AUTCharacter* UTChar = Cast<AUTCharacter>(NewDriver);
	if (bDead || bEntryLocked || NewDriver == nullptr || NewDriver->Controller == nullptr || UTChar == nullptr || Driver != nullptr)
	{
		return false;
	}
	if (!IsInEntryRange(NewDriver))
	{
		return false;
	}

	// Team check
	if (TeamNum != 255)
	{
		AUTPlayerState* PS = Cast<AUTPlayerState>(UTChar->PlayerState);
		if (PS != nullptr && PS->GetTeamNum() != TeamNum)
		{
			return false;
		}
	}

	return true;
}

void UUTVehicleComponent::SetEntryLocked(bool bLocked)
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr || Owner->Role != ROLE_Authority || bEntryLocked == bLocked)
	{
		return;
	}

	bEntryLocked = bLocked;
	RefreshEntryInput();
	Owner->ForceNetUpdate();
}

bool UUTVehicleComponent::DriverEnter(APawn* NewDriver)
{
	AActor* Owner = GetOwner();
	APawn* VehiclePawn = Cast<APawn>(Owner);
	if (Owner == nullptr || Owner->Role != ROLE_Authority || VehiclePawn == nullptr)
	{
		return false;
	}
	if (!CanEnterVehicle(NewDriver))
	{
		return false;
	}

	AController* C = NewDriver->Controller;
	if (C == nullptr)
	{
		return false;
	}

	Driver = NewDriver;
	UnbindEntryInput();
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

	return true;
}

bool UUTVehicleComponent::DriverLeave(bool bForceLeave)
{
	return DriverLeaveInternal(bForceLeave, false, FVector::ZeroVector, false);
}

bool UUTVehicleComponent::EjectDriver(const FVector& EjectVelocity, bool bInheritVehicleVelocity)
{
	return DriverLeaveInternal(true, true, EjectVelocity, bInheritVehicleVelocity);
}

bool UUTVehicleComponent::DriverLeaveInternal(bool bForceLeave, bool bEject,
	const FVector& EjectVelocity, bool bInheritVehicleVelocity)
{
	AActor* Owner = GetOwner();
	APawn* VehiclePawn = Cast<APawn>(Owner);
	if (Owner == nullptr || Owner->Role != ROLE_Authority || VehiclePawn == nullptr)
	{
		return false;
	}

	APawn* OldDriver = Driver;
	AController* C = VehiclePawn->Controller;
	if (OldDriver == nullptr || C == nullptr)
	{
		// Never clear Driver without a controller available to restore possession;
		// doing so strands a hidden, collision-disabled character on disconnect.
		return false;
	}

	AUTCharacter* UTChar = Cast<AUTCharacter>(OldDriver);
	FVector ExitLocation = FVector::ZeroVector;
	FRotator ExitRotation(0.0f, Owner->GetActorRotation().Yaw, 0.0f);
	bool bHasExitLocation = false;

	if (bEject && UTChar != nullptr)
	{
		FVector BoundsOrigin = Owner->GetActorLocation();
		FVector BoundsExtent = FVector::ZeroVector;
		UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(Owner->GetRootComponent());
		if (RootPrimitive != nullptr)
		{
			BoundsOrigin = RootPrimitive->Bounds.Origin;
			BoundsExtent = RootPrimitive->Bounds.BoxExtent;
		}
		else
		{
			Owner->GetActorBounds(true, BoundsOrigin, BoundsExtent);
		}
		const float CapsuleHalfHeight = UTChar->GetCapsuleComponent() != nullptr
			? UTChar->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
			: 96.0f;
		const FVector RequestedLocation = BoundsOrigin +
			FVector::UpVector * (BoundsExtent.Z + CapsuleHalfHeight + 50.0f);
		ExitLocation = RequestedLocation;
		if (GetWorld() != nullptr)
		{
			FVector AdjustedLocation = RequestedLocation;
			if (GetWorld()->FindTeleportSpot(UTChar, AdjustedLocation, ExitRotation))
			{
				ExitLocation = AdjustedLocation;
			}
		}
		bHasExitLocation = true;
	}
	else if (UTChar != nullptr)
	{
		bHasExitLocation = FindSafeExitLocation(UTChar, ExitLocation, ExitRotation);
	}

	if (!bHasExitLocation && !bForceLeave)
	{
		UE_LOG(LogTemp, Warning, TEXT("[VehicleExit] No supported, collision-free exit for Vehicle=%s Driver=%s; staying in vehicle"),
			*GetNameSafe(Owner), *GetNameSafe(OldDriver));
		return false;
	}
	if (!bHasExitLocation && UTChar != nullptr)
	{
		// A destroyed vehicle must release its driver even when airborne. Put the
		// forced exit above the chassis; manual exits never use this fallback.
		FVector BoundsOrigin;
		FVector BoundsExtent;
		Owner->GetActorBounds(true, BoundsOrigin, BoundsExtent);
		const float CapsuleHalfHeight = UTChar->GetCapsuleComponent() != nullptr
			? UTChar->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
			: 96.0f;
		ExitLocation = BoundsOrigin + FVector::UpVector * (BoundsExtent.Z + CapsuleHalfHeight + 50.0f);
		bHasExitLocation = true;
	}

	AUTVehicle* Vehicle = Cast<AUTVehicle>(VehiclePawn);
	if (Vehicle != nullptr)
	{
		Vehicle->PlayExitSound();
	}

	// Handle spectators viewing this vehicle
	UWorld* World = GetWorld();
	AUTGameState* GS = World != nullptr ? World->GetGameState<AUTGameState>() : nullptr;
	if (World != nullptr && C->PlayerState != nullptr && GS != nullptr &&
		!GS->IsMatchIntermission() && !GS->HasMatchEnded())
	{
		for (FLocalPlayerIterator It(GEngine, World); It; ++It)
		{
			AUTPlayerController* UTPC = Cast<AUTPlayerController>(It->PlayerController);
			if (UTPC != nullptr && UTPC->LastSpectatedPlayerState == C->PlayerState)
			{
				UTPC->ViewPawn(OldDriver);
			}
		}
	}

	// Transfer possession back: vehicle -> character
	C->UnPossess();
	OldDriver->SetOwner(C);

	if (UTChar != nullptr)
	{
		UTChar->StopDriving(VehiclePawn);

		// The location was checked while the hidden driver still belonged to the
		// vehicle. Move there before restoring collision and character visuals.
		UTChar->TeleportTo(ExitLocation, ExitRotation, false, true);

		UTChar->MaxSafeFallSpeed = SavedMaxSafeFallSpeed;
		SetDriverVisualState(UTChar, false);

		if (UTChar->GetWeapon() != nullptr)
		{
			UTChar->GetWeapon()->AddAmmo(0);
		}
	}

	C->Possess(OldDriver);
	VehiclePawn->PlayerState = nullptr;

	if (UTChar != nullptr && UTChar->GetCharacterMovement() != nullptr)
	{
		if (bEject)
		{
			FVector LaunchVelocity = EjectVelocity;
			if (bInheritVehicleVelocity)
			{
				const FVector VehicleVelocity = Owner->GetVelocity();
				LaunchVelocity.X += VehicleVelocity.X;
				LaunchVelocity.Y += VehicleVelocity.Y;
			}
			UTChar->LaunchCharacter(LaunchVelocity, true, true);
		}
		else
		{
			UTChar->GetCharacterMovement()->Velocity = FVector::ZeroVector;
			UTChar->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
	}

	VisualDriver = nullptr;
	Driver = nullptr;
	if (Owner->Role == ROLE_Authority)
	{
		// Occupied vehicles are owned by their driver so exit RPCs are legal.
		// Empty vehicles are never reassigned to an arbitrary overlapper.
		Owner->SetOwner(nullptr);
	}
	return true;
}

bool UUTVehicleComponent::FindSafeExitLocation(AUTCharacter* Character, FVector& OutLocation, FRotator& OutRotation) const
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	UCapsuleComponent* Capsule = Character != nullptr ? Character->GetCapsuleComponent() : nullptr;
	if (Owner == nullptr || World == nullptr || Character == nullptr || Capsule == nullptr)
	{
		return false;
	}

	FVector BoundsOrigin;
	FVector BoundsExtent;
	Owner->GetActorBounds(true, BoundsOrigin, BoundsExtent);
	const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Forward = Owner->GetActorForwardVector().GetSafeNormal2D();
	const FVector Right = Owner->GetActorRightVector().GetSafeNormal2D();
	const FVector Directions[] = { Right, -Right, -Forward, Forward };

	FCollisionQueryParams TraceParams(FName(TEXT("VehicleExitGround")), false, Owner);
	TraceParams.AddIgnoredActor(Character);
	OutRotation = FRotator(0.0f, Owner->GetActorRotation().Yaw, 0.0f);

	for (const FVector& Direction : Directions)
	{
		// Project the world AABB onto the requested exit direction so large flyers
		// clear their wings while compact cars do not throw the driver far away.
		const float ProjectedExtent = FMath::Abs(Direction.X) * BoundsExtent.X + FMath::Abs(Direction.Y) * BoundsExtent.Y;
		const FVector PlanarLocation = BoundsOrigin + Direction * (ProjectedExtent + CapsuleRadius + 60.0f);
		const FVector TraceStart(PlanarLocation.X, PlanarLocation.Y,
			BoundsOrigin.Z + BoundsExtent.Z + CapsuleHalfHeight + 150.0f);
		const FVector TraceEnd(PlanarLocation.X, PlanarLocation.Y,
			BoundsOrigin.Z - BoundsExtent.Z - 1200.0f);

		FHitResult GroundHit;
		if (!World->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_WorldStatic, TraceParams))
		{
			continue;
		}

		const FVector RequestedLocation = GroundHit.ImpactPoint + FVector::UpVector * (CapsuleHalfHeight + 4.0f);
		FVector AdjustedLocation = RequestedLocation;
		if (World->FindTeleportSpot(Character, AdjustedLocation, OutRotation) &&
			FVector::DistSquared(AdjustedLocation, RequestedLocation) <= FMath::Square(200.0f))
		{
			OutLocation = AdjustedLocation;
			UE_LOG(LogTemp, Warning, TEXT("[VehicleExit] Safe exit Vehicle=%s Driver=%s Location=%s Ground=%s"),
				*GetNameSafe(Owner), *GetNameSafe(Character), *OutLocation.ToString(), *GetNameSafe(GroundHit.GetActor()));
			return true;
		}
	}

	return false;
}

float UUTVehicleComponent::ApplyDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr || Owner->Role != ROLE_Authority || bDead || !FMath::IsFinite(Damage) || Damage <= 0.0f)
	{
		return 0.0f;
	}

	const float ScaledDamage = CalculateVehicleDamage(Damage, DamageEvent);
	const int32 AppliedDamage = FMath::Max(0, FMath::TruncToInt(ScaledDamage));
	if (AppliedDamage == 0)
	{
		return 0.0f;
	}

	Health -= AppliedDamage;
	if (Health <= 0)
	{
		Health = 0;
		VehicleDied(EventInstigator);
	}

	UE_LOG(LogTemp, Verbose, TEXT("[VehicleDamage] Vehicle=%s Type=%s Raw=%.2f Scaled=%.2f Applied=%d Health=%d/%d"),
		*GetNameSafe(Owner), *GetNameSafe(DamageEvent.DamageTypeClass.Get()), Damage, ScaledDamage, AppliedDamage, Health, HealthMax);

	return static_cast<float>(AppliedDamage);
}

float UUTVehicleComponent::CalculateVehicleDamage(float Damage, const FDamageEvent& DamageEvent) const
{
	if (const UUTVehicleDamageType* VehicleDamageType = Cast<UUTVehicleDamageType>(DamageEvent.DamageTypeClass.GetDefaultObject()))
	{
		return VehicleDamageType->CalculateVehicleDamage(Damage, this);
	}

	const float SafeDefaultScaling = FMath::IsFinite(DefaultVehicleDamageScaling)
		? FMath::Max(0.0f, DefaultVehicleDamageScaling)
		: 1.0f;
	const float Scaling = GetStockVehicleDamageScaling(DamageEvent.DamageTypeClass.Get(), bLightArmor, SafeDefaultScaling);
	const float ScaledDamage = Damage * Scaling;
	return FMath::IsFinite(ScaledDamage) ? FMath::Max(0.0f, ScaledDamage) : 0.0f;
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
	if (Driver != nullptr)
	{
		UnbindEntryInput();
	}

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

void UUTVehicleComponent::OnRep_EntryLocked()
{
	RefreshEntryInput();
}

void UUTVehicleComponent::RefreshEntryInput()
{
	if (EntryInputComponent != nullptr &&
		(EntryInputPawn == nullptr || !CanEnterVehicle(EntryInputPawn)))
	{
		UnbindEntryInput();
	}

	if (EntryInputComponent == nullptr && !bEntryLocked && Driver == nullptr && !bDead)
	{
		for (APawn* Candidate : OverlappingPawns)
		{
			if (Candidate != nullptr && Candidate->IsLocallyControlled() && CanEnterVehicle(Candidate))
			{
				BindEntryInput(Candidate);
				break;
			}
		}
	}
}

void UUTVehicleComponent::DrawEntryPrompt(AUTHUD* HUD, UCanvas* Canvas, APlayerController* ViewingPC)
{
	if (Canvas == nullptr || HUD == nullptr || ViewingPC == nullptr)
	{
		return;
	}

	APawn* ViewingPawn = ViewingPC->GetPawn();
	if (!CanEnterVehicle(ViewingPawn))
	{
		return;
	}

	FFormatNamedArguments PromptArgs;
	PromptArgs.Add(TEXT("Key"), FText::FromString(ResolveVehicleUseKey(ViewingPC)));
	const FText Prompt = FText::Format(
		NSLOCTEXT("UTVehicles", "EnterVehiclePrompt", "Press {Key} to enter vehicle"),
		PromptArgs);
	// Match Blitz's GameMessages announcement treatment: native UT medium font,
	// white text, a small black shadow, and the 0.68 announcement slot.
	UFont* PromptFont = HUD->GetFontFromSizeIndex(2);
	if (PromptFont == nullptr)
	{
		return;
	}

	float TextWidth = 0.0f;
	float TextHeight = 0.0f;
	Canvas->TextSize(PromptFont, Prompt.ToString(), TextWidth, TextHeight);
	const float TextX = (Canvas->ClipX - TextWidth) * 0.5f;
	const float TextY = Canvas->ClipY * 0.68f;
	FCanvasTextItem PromptItem(FVector2D(TextX, TextY), Prompt, PromptFont, FLinearColor::White);
	PromptItem.EnableShadow(FLinearColor::Black, FVector2D(1.0f, 2.0f));
	Canvas->DrawItem(PromptItem);
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

	// Occupants use the same remappable ActivateSpecial binding as vehicle entry.
	// A boosting Scorpion replaces the ordinary exit hint with an unmistakable
	// armed-eject prompt only when pressing the key will actually arm it.
	APawn* VehiclePawn = Cast<APawn>(GetOwner());
	if (ViewingPC == nullptr || VehiclePawn == nullptr || ViewingPC->GetPawn() != VehiclePawn)
	{
		return;
	}

	const AUTVehicle_Scorpion* Scorpion = Cast<AUTVehicle_Scorpion>(VehiclePawn);
	// Driver replication can trail possession by a frame, so possession is the
	// reliable local HUD gate. Show EJECT for the entire replicated boost window;
	// readiness remains the server's authoritative action gate.
	const bool bShowEjectPrompt = Scorpion != nullptr && Scorpion->ShouldShowBoostEjectPrompt();
	FFormatNamedArguments PromptArgs;
	PromptArgs.Add(TEXT("Key"), FText::FromString(ResolveVehicleUseKey(ViewingPC)));
	const FText UsePrompt = FText::Format(
		bShowEjectPrompt
			? NSLOCTEXT("UTVehicles", "EjectScorpionPrompt", "Press {Key} to EJECT")
			: NSLOCTEXT("UTVehicles", "ExitVehiclePrompt", "Press {Key} to exit vehicle"),
		PromptArgs);
	UFont* PromptFont = HUD->GetFontFromSizeIndex(bShowEjectPrompt ? 3 : 2);
	if (PromptFont == nullptr)
	{
		return;
	}

	float PromptWidth = 0.0f;
	float PromptHeight = 0.0f;
	Canvas->TextSize(PromptFont, UsePrompt.ToString(), PromptWidth, PromptHeight);
	const FVector2D PromptPosition((ScreenWidth - PromptWidth) * 0.5f,
		ScreenHeight * (bShowEjectPrompt ? 0.58f : 0.68f));
	// UT3 and Blitz both use clean white announcement text rather than a colored
	// widget. Size/position still make the time-sensitive eject prompt distinct.
	FCanvasTextItem PromptItem(PromptPosition, UsePrompt, PromptFont, FLinearColor::White);
	PromptItem.EnableShadow(FLinearColor::Black, FVector2D(1.0f, 2.0f));
	Canvas->DrawItem(PromptItem);
}
