#include "UTVehicleFlying.h"
#include "UnrealTournament.h"
#include "UTCharacter.h"
#include "UTPlayerController.h"
#include "UTHUD.h"
#include "Components/InputComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"

AUTVehicleFlying::AUTVehicleFlying(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Skeletal mesh as root
	VehicleMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("VehicleMesh"));
	RootComponent = VehicleMesh;

	// Hover movement
	HoverMovement = CreateDefaultSubobject<UUTVehicleMovementHover>(TEXT("HoverMovement"));
	HoverMovement->UpdatedComponent = VehicleMesh;

	// Vehicle component
	VehicleComponent = CreateDefaultSubobject<UUTVehicleComponent>(TEXT("VehicleComponent"));

	// Camera setup
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(VehicleMesh);
	SpringArm->TargetArmLength = 700.0f;
	SpringArm->SetRelativeLocation(FVector(0.0f, 0.0f, 150.0f));
	SpringArm->bUsePawnControlRotation = true;
	SpringArm->bInheritPitch = true;
	SpringArm->bInheritYaw = true;
	SpringArm->bInheritRoll = false;
	SpringArm->bDoCollisionTest = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);

	SetReplicates(true);
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true;

	bLiftUp = false;
	bLiftDown = false;
	BoundInputPC = nullptr;
	DrivingInputComponent = nullptr;
	VehicleCameraActor = nullptr;
	ThrottleAxisValue = 0.0f;
	ReverseAxisValue = 0.0f;
	SteerRightAxisValue = 0.0f;
	SteerLeftAxisValue = 0.0f;
	bPrimaryFireKeyDown = false;
	bAltFireKeyDown = false;
	bPrimaryFireInputDown = false;
	bAltFireInputDown = false;
}

uint8 AUTVehicleFlying::GetTeamNum() const
{
	if (VehicleComponent != nullptr)
	{
		return VehicleComponent->TeamNum;
	}
	return 255;
}

void AUTVehicleFlying::SetTeamForSideSwap_Implementation(uint8 NewTeamNum)
{
	if (VehicleComponent != nullptr)
	{
		VehicleComponent->TeamNum = NewTeamNum;
	}
}

UPawnMovementComponent* AUTVehicleFlying::GetMovementComponent() const
{
	return HoverMovement;
}

void AUTVehicleFlying::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	// AUTPlayerController consumes UT's movement bindings before a pawn-level
	// component sees them. BindDrivingInput pushes a dedicated component above
	// the controller, matching the working wheeled-vehicle input path.
}

void AUTVehicleFlying::BindDrivingInput()
{
	APlayerController* PC = Cast<APlayerController>(Controller);
	UE_LOG(LogTemp, Warning, TEXT("[VehicleFlyingInput] Bind attempt Vehicle=%s Role=%d Controller=%s Local=%d"),
		*GetName(), (int32)Role, *GetNameSafe(Controller),
		PC != nullptr && PC->IsLocalController() ? 1 : 0);
	if (PC == nullptr || !PC->IsLocalController() || PC->PlayerInput == nullptr)
	{
		return;
	}
	if (BoundInputPC == PC && DrivingInputComponent != nullptr)
	{
		if (PC->MyHUD != nullptr)
		{
			PC->MyHUD->AddPostRenderedActor(this);
		}
		return;
	}

	UnbindDrivingInput();
	BoundInputPC = PC;
	DrivingInputComponent = NewObject<UInputComponent>(PC);
	if (DrivingInputComponent == nullptr)
	{
		BoundInputPC = nullptr;
		return;
	}

	DrivingInputComponent->Priority = 10000;
	DrivingInputComponent->bBlockInput = false;
	UInputComponent* IC = DrivingInputComponent;
	IC->BindAxis("MoveForward", this, &AUTVehicleFlying::OnThrottleInput);
	IC->BindAxis("MoveBackward", this, &AUTVehicleFlying::OnReverseInput);
	IC->BindAxis("MoveRight", this, &AUTVehicleFlying::OnSteeringInput);
	IC->BindAxis("MoveLeft", this, &AUTVehicleFlying::OnSteerLeftInput);
	// Leave Turn/LookUp to AUTPlayerController so its replicated control
	// rotation drives the Raptor's aim and desired flight heading. Individual
	// flyers may keep their chase boom fixed to the vehicle for framing.
	IC->BindAction("Jump", IE_Pressed, this, &AUTVehicleFlying::OnLiftUp);
	IC->BindAction("Jump", IE_Released, this, &AUTVehicleFlying::OnLiftUpRelease);
	IC->BindAction("Crouch", IE_Pressed, this, &AUTVehicleFlying::OnLiftDown);
	IC->BindAction("Crouch", IE_Released, this, &AUTVehicleFlying::OnLiftDownRelease);
	// Observe press state without replacing AUTPlayerController's bookkeeping or
	// deferred dispatch. Direct release supplies its missing non-character stop.
	FInputActionBinding& PrimaryFirePressed = IC->BindAction(
		"StartFire", IE_Pressed, this, &AUTVehicleFlying::HandlePrimaryFirePressed);
	PrimaryFirePressed.bConsumeInput = false;
	FInputActionBinding& PrimaryFireReleased = IC->BindAction(
		"StopFire", IE_Released, this, &AUTVehicleFlying::HandlePrimaryFireReleased);
	PrimaryFireReleased.bConsumeInput = false;
	FInputActionBinding& AltFirePressed = IC->BindAction(
		"StartAltFire", IE_Pressed, this, &AUTVehicleFlying::HandleAltFirePressed);
	AltFirePressed.bConsumeInput = false;
	FInputActionBinding& AltFireReleased = IC->BindAction(
		"StopAltFire", IE_Released, this, &AUTVehicleFlying::HandleAltFireReleased);
	AltFireReleased.bConsumeInput = false;
	FInputKeyBinding& HornBinding = IC->BindKey(EKeys::H, IE_Pressed, this, &AUTVehicleFlying::OnHornPressed);
	HornBinding.bConsumeInput = true;
	FInputActionBinding& ExitBinding = IC->BindAction(
		"ActivateSpecial", IE_Pressed, this, &AUTVehicleFlying::HandleActivateSpecialPressed);
	ExitBinding.bConsumeInput = true;
	BindVehicleSpecificInput(IC);
	IC->RegisterComponent();
	PC->PushInputComponent(IC);
	if (PC->MyHUD != nullptr)
	{
		PC->MyHUD->AddPostRenderedActor(this);
	}
	UE_LOG(LogTemp, Warning, TEXT("[VehicleFlyingInput] Capture pushed PC=%s Component=%s Priority=%d"),
		*PC->GetName(), *IC->GetName(), IC->Priority);
}

void AUTVehicleFlying::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HoverMovement != nullptr)
	{
		const bool bShouldPark = VehicleComponent == nullptr ||
			!VehicleComponent->HasDriver() || VehicleComponent->bDead;
		HoverMovement->SetParkingMode(bShouldPark);
	}

	// UT's camera manager may replace the view target after possession callbacks
	// because this is a non-character pawn. The wheeled path already repairs
	// that on tick; flyers need the same guard or the view falls back to the
	// Raptor pawn origin (the ground-level/vehicle-invisible camera).
	APlayerController* PC = Cast<APlayerController>(Controller);
	if (PC != nullptr && PC->IsLocalController() && VehicleCameraActor != nullptr &&
		PC->GetViewTarget() != VehicleCameraActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[VehicleFlyingCamera] Restoring displaced view target Old=%s New=%s"),
			*GetNameSafe(PC->GetViewTarget()), *GetNameSafe(VehicleCameraActor));
		PC->SetViewTarget(VehicleCameraActor);
	}
}

void AUTVehicleFlying::ActivateVehicleCamera()
{
	APlayerController* PC = Cast<APlayerController>(Controller);
	if (PC == nullptr || !PC->IsLocalController() || GetWorld() == nullptr || SpringArm == nullptr || Camera == nullptr)
	{
		return;
	}

	SpringArm->Activate(true);
	Camera->Activate(true);
	FRotator InitialView = GetActorRotation();
	InitialView.Pitch = -10.0f;
	InitialView.Roll = 0.0f;
	PC->SetControlRotation(InitialView);

	ACameraActor* CameraActor = Cast<ACameraActor>(VehicleCameraActor);
	if (CameraActor == nullptr)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = PC;
		SpawnParameters.Instigator = this;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		CameraActor = GetWorld()->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(), Camera->GetComponentLocation(), Camera->GetComponentRotation(), SpawnParameters);
		VehicleCameraActor = CameraActor;
	}

	if (CameraActor == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("[VehicleFlyingCamera] Failed to spawn CameraActor for %s"), *GetName());
		return;
	}

	CameraActor->SetActorEnableCollision(false);
	CameraActor->AttachToComponent(Camera, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	if (CameraActor->GetCameraComponent() != nullptr)
	{
		CameraActor->GetCameraComponent()->SetFieldOfView(Camera->FieldOfView);
		CameraActor->GetCameraComponent()->bConstrainAspectRatio = false;
	}
	PC->SetViewTargetWithBlend(CameraActor, 0.2f);
	UE_LOG(LogTemp, Warning, TEXT("[VehicleFlyingCamera] CameraActor view PC=%s Target=%s Pivot=%s Camera=%s Arm=%.1f Rotation=%s"),
		*PC->GetName(), *CameraActor->GetName(), *SpringArm->GetComponentLocation().ToString(),
		*CameraActor->GetActorLocation().ToString(), SpringArm->TargetArmLength,
		*CameraActor->GetActorRotation().ToString());
}

void AUTVehicleFlying::DeactivateVehicleCamera()
{
	APlayerController* PC = BoundInputPC != nullptr ? BoundInputPC : Cast<APlayerController>(Controller);
	if (PC != nullptr && (PC->GetViewTarget() == VehicleCameraActor || PC->GetViewTarget() == this))
	{
		AActor* ReturnTarget = VehicleComponent != nullptr ? VehicleComponent->Driver : nullptr;
		if (ReturnTarget != nullptr)
		{
			PC->SetViewTarget(ReturnTarget);
		}
	}
	if (VehicleCameraActor != nullptr)
	{
		VehicleCameraActor->Destroy();
		VehicleCameraActor = nullptr;
	}
}


void AUTVehicleFlying::UnbindDrivingInput()
{
	HandlePrimaryFireReleased();
	HandleAltFireReleased();

	if (BoundInputPC != nullptr && BoundInputPC->MyHUD != nullptr)
	{
		BoundInputPC->MyHUD->RemovePostRenderedActor(this);
	}
	if (BoundInputPC != nullptr && DrivingInputComponent != nullptr)
	{
		BoundInputPC->PopInputComponent(DrivingInputComponent);
	}
	if (DrivingInputComponent != nullptr)
	{
		DrivingInputComponent->DestroyComponent();
	}
	DrivingInputComponent = nullptr;
	BoundInputPC = nullptr;
	ThrottleAxisValue = 0.0f;
	ReverseAxisValue = 0.0f;
	SteerRightAxisValue = 0.0f;
	SteerLeftAxisValue = 0.0f;
	bLiftUp = false;
	bLiftDown = false;
	if (HoverMovement != nullptr)
	{
		HoverMovement->SetThrottleInput(0.0f);
		HoverMovement->SetSteeringInput(0.0f);
		HoverMovement->SetLiftInput(0.0f);
		HoverMovement->SetPitchInput(0.0f);
	}
}

void AUTVehicleFlying::ApplyFlightInput()
{
	if (HoverMovement != nullptr)
	{
		if (VehicleComponent != nullptr && VehicleComponent->HasDriver() && !VehicleComponent->bDead)
		{
			HoverMovement->SetParkingMode(false);
		}
		HoverMovement->SetThrottleInput(FMath::Clamp(ThrottleAxisValue - ReverseAxisValue, -1.0f, 1.0f));
		HoverMovement->SetSteeringInput(FMath::Clamp(SteerRightAxisValue - SteerLeftAxisValue, -1.0f, 1.0f));
	}
}

void AUTVehicleFlying::OnThrottleInput(float Value)
{
	ThrottleAxisValue = Value;
	ApplyFlightInput();
}

void AUTVehicleFlying::OnReverseInput(float Value)
{
	ReverseAxisValue = Value;
	ApplyFlightInput();
}

void AUTVehicleFlying::OnSteeringInput(float Value)
{
	SteerRightAxisValue = Value;
	ApplyFlightInput();
}

void AUTVehicleFlying::OnSteerLeftInput(float Value)
{
	SteerLeftAxisValue = Value;
	ApplyFlightInput();
}

void AUTVehicleFlying::OnLiftUp()
{
	bLiftUp = true;
	if (HoverMovement != nullptr)
	{
		HoverMovement->SetLiftInput(1.0f);
	}
}

void AUTVehicleFlying::OnLiftUpRelease()
{
	bLiftUp = false;
	if (HoverMovement != nullptr)
	{
		HoverMovement->SetLiftInput(bLiftDown ? -1.0f : 0.0f);
	}
}

void AUTVehicleFlying::OnLiftDown()
{
	bLiftDown = true;
	if (HoverMovement != nullptr)
	{
		HoverMovement->SetLiftInput(-1.0f);
	}
}

void AUTVehicleFlying::OnLiftDownRelease()
{
	bLiftDown = false;
	if (HoverMovement != nullptr)
	{
		HoverMovement->SetLiftInput(bLiftUp ? 1.0f : 0.0f);
	}
}

void AUTVehicleFlying::OnHornPressed()
{
	if (VehicleComponent != nullptr)
	{
		VehicleComponent->RequestHorn();
	}
}

void AUTVehicleFlying::OnMousePitch(float Value)
{
	if (HoverMovement != nullptr)
	{
		HoverMovement->SetPitchInput(Value);
	}
}

void AUTVehicleFlying::OnPrimaryFirePressed()
{
}

void AUTVehicleFlying::OnPrimaryFireReleased()
{
}

void AUTVehicleFlying::OnAltFirePressed()
{
}

void AUTVehicleFlying::OnAltFireReleased()
{
}

void AUTVehicleFlying::HandlePrimaryFirePressed()
{
	bPrimaryFireKeyDown = true;
}

void AUTVehicleFlying::HandlePrimaryFireReleased()
{
	bPrimaryFireKeyDown = false;
	StopVehicleFire(0);
}

void AUTVehicleFlying::HandleAltFirePressed()
{
	bAltFireKeyDown = true;
}

void AUTVehicleFlying::HandleAltFireReleased()
{
	bAltFireKeyDown = false;
	StopVehicleFire(1);
}

void AUTVehicleFlying::BindVehicleSpecificInput(UInputComponent*)
{
}

void AUTVehicleFlying::PawnStartFire(uint8 FireModeNum)
{
	if (FireModeNum == 0)
	{
		if (!bPrimaryFireInputDown && (BoundInputPC == nullptr || bPrimaryFireKeyDown))
		{
			bPrimaryFireInputDown = true;
			OnPrimaryFirePressed();
		}
	}
	else if (FireModeNum == 1)
	{
		if (!bAltFireInputDown && (BoundInputPC == nullptr || bAltFireKeyDown))
		{
			bAltFireInputDown = true;
			OnAltFirePressed();
		}
	}
	else
	{
		Super::PawnStartFire(FireModeNum);
	}
}

void AUTVehicleFlying::StopVehicleFire(uint8 FireModeNum)
{
	if (FireModeNum == 0 && bPrimaryFireInputDown)
	{
		bPrimaryFireInputDown = false;
		OnPrimaryFireReleased();
	}
	else if (FireModeNum == 1 && bAltFireInputDown)
	{
		bAltFireInputDown = false;
		OnAltFireReleased();
	}
}

float AUTVehicleFlying::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (VehicleComponent != nullptr)
	{
		float ResolvedDamage = Damage;
		if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
		{
			ResolvedDamage = InternalTakeRadialDamage(Damage,
				static_cast<const FRadialDamageEvent&>(DamageEvent), EventInstigator, DamageCauser);
		}
		else if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
		{
			ResolvedDamage = InternalTakePointDamage(Damage,
				static_cast<const FPointDamageEvent&>(DamageEvent), EventInstigator, DamageCauser);
		}

		return VehicleComponent->ApplyDamage(ResolvedDamage, DamageEvent, EventInstigator, DamageCauser);
	}
	return 0.0f;
}

void AUTVehicleFlying::PawnClientRestart()
{
	Super::PawnClientRestart();
	if (HoverMovement != nullptr)
	{
		HoverMovement->SetParkingMode(false);
	}
	if (Camera != nullptr)
	{
		Camera->Activate();
	}
	ActivateVehicleCamera();
	BindDrivingInput();
}

void AUTVehicleFlying::UnPossessed()
{
	DeactivateVehicleCamera();
	UnbindDrivingInput();
	if (HoverMovement != nullptr)
	{
		HoverMovement->SetParkingMode(true);
	}
	Super::UnPossessed();
}

void AUTVehicleFlying::OnRep_Controller()
{
	Super::OnRep_Controller();
	if (Controller == nullptr)
	{
		DeactivateVehicleCamera();
		UnbindDrivingInput();
		if (HoverMovement != nullptr)
		{
			HoverMovement->SetParkingMode(true);
		}
	}
	else if (IsLocallyControlled())
	{
		if (HoverMovement != nullptr)
		{
			HoverMovement->SetParkingMode(false);
		}
		ActivateVehicleCamera();
		BindDrivingInput();
	}
}

void AUTVehicleFlying::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DeactivateVehicleCamera();
	UnbindDrivingInput();
	Super::EndPlay(EndPlayReason);
}

void AUTVehicleFlying::PostRender(AUTHUD* HUD, UCanvas* Canvas)
{
	if (VehicleComponent != nullptr && Controller != nullptr)
	{
		APlayerController* PC = Cast<APlayerController>(Controller);
		VehicleComponent->DrawVehicleHUD(HUD, Canvas, PC);
	}
}

void AUTVehicleFlying::PostRenderFor(APlayerController* PC, UCanvas* Canvas, FVector CameraPosition, FVector CameraDir)
{
	Super::PostRenderFor(PC, Canvas, CameraPosition, CameraDir);
	if (VehicleComponent != nullptr && PC != nullptr)
	{
		VehicleComponent->DrawEntryPrompt(Cast<AUTHUD>(PC->MyHUD), Canvas, PC);
	}
}

bool AUTVehicleFlying::ServerDriverLeave_Validate()
{
	return true;
}

void AUTVehicleFlying::ServerDriverLeave_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("[VehicleFlyingExit] Server request Vehicle=%s Controller=%s Driver=%s"),
		*GetName(), *GetNameSafe(Controller),
		*GetNameSafe(VehicleComponent != nullptr ? VehicleComponent->Driver : nullptr));
	HandleDriverLeaveRequest();
}

void AUTVehicleFlying::HandleActivateSpecialPressed()
{
	UE_LOG(LogTemp, Warning, TEXT("[VehicleFlyingExit] ActivateSpecial pressed Vehicle=%s Role=%d Local=%d Controller=%s"),
		*GetName(), (int32)Role, IsLocallyControlled() ? 1 : 0, *GetNameSafe(Controller));
	if (Role == ROLE_Authority)
	{
		HandleDriverLeaveRequest();
	}
	else
	{
		ServerDriverLeave();
	}
}

bool AUTVehicleFlying::HandleDriverLeaveRequest()
{
	return VehicleComponent != nullptr && VehicleComponent->DriverLeave(false);
}
