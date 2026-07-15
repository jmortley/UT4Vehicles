#include "UTVehicle.h"
#include "UTVehicleDamageType.h"
#include "UTVehicleMeshComponent.h"
#include "UnrealTournament.h"
#include "WheeledVehicleMovementComponent.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "Components/AudioComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerInput.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "UTCharacter.h"
#include "UTGameState.h"
#include "UTPlayerController.h"
#include "UTHUD.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "VehicleWheel.h"

AUTVehicle::AUTVehicle(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UUTVehicleMeshComponent>(AWheeledVehicle::VehicleMeshComponentName))
{
	VehicleComponent = CreateDefaultSubobject<UUTVehicleComponent>(TEXT("VehicleComponent"));
	// This plugin keeps only the root chassis rigid body. The component follows
	// that body directly; skeletal physics blending would only overwrite the
	// code-driven visual wheel bones later in the frame.
	GetMesh()->bBlendPhysics = false;

	// Camera setup
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(GetMesh());
	SpringArm->TargetArmLength = 550.0f;
	SpringArm->SetRelativeLocation(FVector(0.0f, 0.0f, 300.0f));
	SpringArm->SetRelativeRotation(FRotator(-10.0f, 0.0f, 0.0f));
	SpringArm->TargetOffset = FVector::ZeroVector;
	// A reliable vehicle chase camera must inherit the chassis yaw. UT's
	// controller rotation is character-centric and can remain aimed in the
	// original world direction while a non-character pawn steers.
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritPitch = true;
	SpringArm->bInheritYaw = true;
	SpringArm->bInheritRoll = false;
	SpringArm->bDoCollisionTest = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);

	EngineAudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("EngineAudio"));
	EngineAudioComponent->SetupAttachment(GetMesh());
	EngineAudioComponent->bAutoActivate = false;
	EngineLoopSound = nullptr;
	EngineStartSound = nullptr;
	EngineStopSound = nullptr;
	ImpactSound = nullptr;
	MinRunOverSpeed = 250.0f;
	RunOverDamageScale = 0.075f;
	RunOverMomentumScale = 0.25f;
	RanOverDamageType = UUTDmgType_RanOver::StaticClass();
	VehicleSoundAttenuation = nullptr;

	SetReplicates(true);
	bAlwaysRelevant = true;

	BoundInputPC = nullptr;
	VehicleCameraActor = nullptr;
	DrivingInputComponent = nullptr;
	ThrottleAxisValue = 0.0f;
	ReverseAxisValue = 0.0f;
	SteerRightAxisValue = 0.0f;
	SteerLeftAxisValue = 0.0f;
	bHandbrakeInputDown = false;
	bPrimaryFireKeyDown = false;
	bAltFireKeyDown = false;
	bPrimaryFireInputDown = false;
	bAltFireInputDown = false;
	NextDriveInputLogTime = 0.0f;
	LastImpactSoundTime = -1000.0f;
	LastRunOverTime = -1000.0f;
	bVacantBrakeApplied = false;
}

void AUTVehicle::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// These must be set after the BodyInstance is fully initialized
	GetMesh()->SetAngularDamping(2.0f);
	GetMesh()->SetLinearDamping(0.1f);

	// Imported UT3 physics assets have a body on every bone. AWheeledVehicle
	// simulates the mesh, so every non-chassis body spawns as a constrained
	// ragdoll body that collides with the ground and fights the PhysX wheel
	// sim. Keep only the root (chassis) body live so any UT3 physics asset
	// works unmodified — no chassis-only asset needed per vehicle.
	USkeletalMeshComponent* VMesh = GetMesh();
	for (FConstraintInstance* Constraint : VMesh->Constraints)
	{
		if (Constraint != nullptr)
		{
			Constraint->TermConstraint();
		}
	}
	FBodyInstance* ChassisBody = VMesh->GetBodyInstance();
	for (FBodyInstance* Body : VMesh->Bodies)
	{
		if (Body != nullptr && Body != ChassisBody && Body->IsValidBodyInstance())
		{
			Body->SetInstanceSimulatePhysics(false);
			Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	// Only the root chassis body remains simulated. Let that body move the
	// component itself, but do not run the skeletal ragdoll blend afterward;
	// it would restore the disabled tire bodies over the visual wheel pose.
	VMesh->bBlendPhysics = false;
	VMesh->SetNotifyRigidBodyCollision(true);
	VMesh->OnComponentHit.AddUniqueDynamic(this, &AUTVehicle::OnVehicleMeshHit);

	// The extracted waves do not carry attenuation assets. Give every vehicle
	// sound a practical 3D falloff so remote cars are positional and do not play
	// at full volume across the entire map.
	VehicleSoundAttenuation = NewObject<USoundAttenuation>(this, TEXT("VehicleSoundAttenuationRuntime"));
	if (VehicleSoundAttenuation != nullptr)
	{
		FAttenuationSettings& Settings = VehicleSoundAttenuation->Attenuation;
		Settings.bAttenuate = true;
		Settings.bSpatialize = true;
		Settings.DistanceAlgorithm = ATTENUATION_Linear;
		Settings.AttenuationShape = EAttenuationShape::Sphere;
		Settings.AttenuationShapeExtents = FVector(600.0f, 0.0f, 0.0f);
		Settings.FalloffDistance = 4500.0f;
		Settings.OmniRadius = 150.0f;
	}

	if (EngineAudioComponent != nullptr && EngineLoopSound != nullptr)
	{
		EngineAudioComponent->bAllowSpatialization = true;
		EngineAudioComponent->bShouldRemainActiveIfDropped = true;
		EngineAudioComponent->AttenuationSettings = VehicleSoundAttenuation;
		EngineAudioComponent->SetSound(EngineLoopSound);
		EngineAudioComponent->SetVolumeMultiplier(0.15f);
		EngineAudioComponent->SetPitchMultiplier(0.70f);
		if (GetWorld() != nullptr && GetWorld()->IsGameWorld())
		{
			EngineAudioComponent->Play();
		}
	}
}

void AUTVehicle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateVacantBrake();
	UpdateVehicleAudio(DeltaSeconds);

	// UT may auto-manage the active view target after possession callbacks.
	// Keep the local driver on the real CameraActor used by the working BP.
	APlayerController* PC = Cast<APlayerController>(Controller);
	if (PC != nullptr && PC->IsLocalController() && VehicleCameraActor != nullptr &&
		PC->GetViewTarget() != VehicleCameraActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[VehicleCamera] Restoring displaced view target Old=%s New=%s"),
			*GetNameSafe(PC->GetViewTarget()), *GetNameSafe(VehicleCameraActor));
		PC->SetViewTarget(VehicleCameraActor);
	}
}

void AUTVehicle::UpdateVehicleAudio(float DeltaSeconds)
{
	if (EngineAudioComponent == nullptr || EngineLoopSound == nullptr || GetWorld() == nullptr || !GetWorld()->IsGameWorld())
	{
		return;
	}
	if (EngineAudioComponent->Sound != EngineLoopSound)
	{
		EngineAudioComponent->SetSound(EngineLoopSound);
	}
	if (!EngineAudioComponent->IsPlaying())
	{
		EngineAudioComponent->Play();
	}

	UWheeledVehicleMovementComponent* Movement = GetVehicleMovementComponent();
	const float RPM = Movement != nullptr ? FMath::Abs(Movement->GetEngineRotationSpeed()) : 0.0f;
	const float RPMAlpha = FMath::Clamp(RPM / 4000.0f, 0.0f, 1.0f);
	const float SpeedAlpha = Movement != nullptr
		? FMath::Clamp(FMath::Abs(Movement->GetForwardSpeed()) / 2300.0f, 0.0f, 1.0f)
		: 0.0f;
	const float LoadAlpha = FMath::Max(RPMAlpha, SpeedAlpha * 0.75f);
	const bool bOccupied = VehicleComponent != nullptr && VehicleComponent->HasDriver();
	const float TargetPitch = FMath::Lerp(0.70f, 1.75f, LoadAlpha);
	const float TargetVolume = bOccupied ? FMath::Lerp(0.35f, 0.90f, LoadAlpha) : 0.15f;

	EngineAudioComponent->SetPitchMultiplier(FMath::FInterpTo(
		EngineAudioComponent->PitchMultiplier, TargetPitch, DeltaSeconds, 7.0f));
	EngineAudioComponent->SetVolumeMultiplier(FMath::FInterpTo(
		EngineAudioComponent->VolumeMultiplier, TargetVolume, DeltaSeconds, 5.0f));
}

void AUTVehicle::PlayEnterSound()
{
	if (EngineStartSound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(this, EngineStartSound, GetActorLocation(),
			1.0f, 1.0f, 0.0f, VehicleSoundAttenuation);
	}
}

void AUTVehicle::PlayExitSound()
{
	if (EngineStopSound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(this, EngineStopSound, GetActorLocation(),
			1.0f, 1.0f, 0.0f, VehicleSoundAttenuation);
	}
}

void AUTVehicle::OnVehicleMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	TryRunOverPawn(OtherActor, Hit);

	if (ImpactSound == nullptr || GetWorld() == nullptr || OtherActor == this)
	{
		return;
	}

	const float ImpactSpeed = FMath::Abs(FVector::DotProduct(GetVelocity(), Hit.ImpactNormal));
	const float ImpulseStrength = NormalImpulse.Size();
	const float Now = GetWorld()->GetTimeSeconds();
	if ((ImpactSpeed < 250.0f && ImpulseStrength < 25000.0f) || Now - LastImpactSoundTime < 0.25f)
	{
		return;
	}

	LastImpactSoundTime = Now;
	const float Volume = FMath::Clamp(FMath::Max(ImpactSpeed / 1400.0f, ImpulseStrength / 180000.0f), 0.20f, 1.0f);
	UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, Hit.ImpactPoint, Volume,
		FMath::FRandRange(0.92f, 1.08f), 0.0f, VehicleSoundAttenuation);
}

void AUTVehicle::TryRunOverPawn(AActor* OtherActor, const FHitResult& Hit)
{
	if (Role != ROLE_Authority || GetWorld() == nullptr || OtherActor == nullptr || OtherActor == this)
	{
		return;
	}

	AUTCharacter* OtherCharacter = Cast<AUTCharacter>(OtherActor);
	if (OtherCharacter == nullptr || OtherCharacter->IsDead() ||
		(VehicleComponent != nullptr && VehicleComponent->Driver == OtherCharacter))
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (LastRunOverPawn.Get() == OtherCharacter && Now - LastRunOverTime < 0.5f)
	{
		return;
	}

	// Center-to-center approach speed works for forward, reverse, and side impacts
	// without mistaking a pawn brushing an already stationary vehicle for roadkill.
	const FVector RelativeVelocity = GetVelocity() - OtherCharacter->GetVelocity();
	const FVector ToPawn = (OtherCharacter->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	const float ApproachSpeed = FVector::DotProduct(RelativeVelocity.GetSafeNormal2D(), ToPawn) *
		RelativeVelocity.Size2D();
	if (ApproachSpeed <= MinRunOverSpeed)
	{
		return;
	}

	AUTGameState* GameState = GetWorld()->GetGameState<AUTGameState>();
	if (GameState != nullptr && GameState->OnSameTeam(this, OtherCharacter))
	{
		return;
	}

	const UCharacterMovementComponent* CharacterMovement = OtherCharacter->GetCharacterMovement();
	const float PawnMass = CharacterMovement != nullptr ? CharacterMovement->Mass : 100.0f;
	const float Damage = FMath::Max(1.0f, ApproachSpeed * RunOverDamageScale);
	const FVector Momentum = RelativeVelocity * (RunOverMomentumScale * PawnMass);
	AController* DamageInstigator = VehicleComponent != nullptr
		? VehicleComponent->DamageInstigator
		: Controller;

	LastRunOverPawn = OtherCharacter;
	LastRunOverTime = Now;
	OtherCharacter->TakeDamage(Damage,
		FUTPointDamageEvent(Damage, Hit, RelativeVelocity.GetSafeNormal(), RanOverDamageType, Momentum),
		DamageInstigator, this);
}

uint8 AUTVehicle::GetTeamNum() const
{
	if (VehicleComponent != nullptr)
	{
		return VehicleComponent->TeamNum;
	}
	return 255;
}

void AUTVehicle::SetTeamForSideSwap_Implementation(uint8 NewTeamNum)
{
	if (VehicleComponent != nullptr)
	{
		VehicleComponent->TeamNum = NewTeamNum;
	}
}

void AUTVehicle::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	// Intentionally no bindings here: the pawn's InputComponent sits below
	// AUTPlayerController's in the input stack, and the controller consumes
	// the movement keys — pawn bindings would only ever receive zeros.
	// BindDrivingInput pushes a dedicated capture component above it.
}

void AUTVehicle::ActivateVehicleCamera()
{
	APlayerController* PC = Cast<APlayerController>(Controller);
	if (PC == nullptr || !PC->IsLocalController() || GetWorld() == nullptr)
	{
		return;
	}

	if (SpringArm == nullptr || Camera == nullptr)
	{
		return;
	}

	// The SpringArm belongs to the possessed vehicle, inherits chassis yaw, and
	// its collision sweep automatically ignores the chassis.
	SpringArm->Activate(true);
	Camera->Activate(true);
	FRotator BehindRotation = GetActorRotation();
	BehindRotation.Pitch = -10.0f;
	BehindRotation.Roll = 0.0f;

	ACameraActor* CameraActor = Cast<ACameraActor>(VehicleCameraActor);
	if (CameraActor == nullptr)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = PC;
		SpawnParameters.Instigator = this;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		CameraActor = GetWorld()->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(), Camera->GetComponentLocation(),
			Camera->GetComponentRotation(), SpawnParameters);
		VehicleCameraActor = CameraActor;
	}

	if (CameraActor == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("[VehicleCamera] Failed to spawn CameraActor for %s"), *GetName());
		return;
	}

	CameraActor->SetActorEnableCollision(false);
	CameraActor->AttachToComponent(Camera, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	if (CameraActor->GetCameraComponent() != nullptr)
	{
		CameraActor->GetCameraComponent()->SetFieldOfView(Camera->FieldOfView);
		CameraActor->GetCameraComponent()->bConstrainAspectRatio = false;
	}

	// This mirrors the working BP: SetViewTargetWithBlend to a real CameraActor.
	// AUTPlayerCameraManager special-cases ACameraActor and will therefore use
	// its camera component instead of forcing the possessed vehicle pawn into
	// UT's FirstPerson camera style.
	PC->SetViewTargetWithBlend(CameraActor, 0.2f);
	UE_LOG(LogTemp, Warning, TEXT("[VehicleCamera] CameraActor view PC=%s Target=%s Pivot=%s Camera=%s Arm=%.1f Rotation=%s"),
		*PC->GetName(), *CameraActor->GetName(),
		*SpringArm->GetComponentLocation().ToString(),
		*CameraActor->GetActorLocation().ToString(), SpringArm->TargetArmLength,
		*BehindRotation.ToString());
}

void AUTVehicle::DeactivateVehicleCamera()
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

void AUTVehicle::BindDrivingInput()
{
	APlayerController* PC = Cast<APlayerController>(Controller);
	UE_LOG(LogTemp, Warning, TEXT("[VehicleInput] Bind attempt Vehicle=%s Role=%d Controller=%s Local=%d PCInput=%s PlayerInput=%s"),
		*GetName(), (int32)Role, *GetNameSafe(Controller), PC != nullptr && PC->IsLocalController() ? 1 : 0,
		PC != nullptr ? *GetNameSafe(PC->InputComponent) : TEXT("NONE"),
		PC != nullptr ? *GetNameSafe(PC->PlayerInput) : TEXT("NONE"));

	if (PC == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[VehicleInput] Bind skipped: Controller is not a PlayerController"));
		return;
	}
	if (!PC->IsLocalController())
	{
		UE_LOG(LogTemp, Warning, TEXT("[VehicleInput] Bind skipped: controller is not local (expected on dedicated server)"));
		return;
	}
	if (PC->PlayerInput == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[VehicleInput] Bind skipped: PlayerInput is null"));
		return;
	}
	if (BoundInputPC == PC && DrivingInputComponent != nullptr)
	{
		return;
	}

	UnbindDrivingInput();
	BoundInputPC = PC;

	// The working BP vehicles spawn an AutoReceiveInput actor. Reproduce that
	// input-stack behavior directly: a pushed component is processed above the
	// controller component that consumes UT's movement bindings.
	DrivingInputComponent = NewObject<UInputComponent>(PC);
	if (DrivingInputComponent == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[VehicleInput] Bind failed: could not create capture InputComponent"));
		BoundInputPC = nullptr;
		return;
	}
	DrivingInputComponent->Priority = 100;
	DrivingInputComponent->bBlockInput = false;
	UInputComponent* IC = DrivingInputComponent;
	IC->BindAxis("MoveForward", this, &AUTVehicle::OnThrottleInput);
	IC->BindAxis("MoveBackward", this, &AUTVehicle::OnReverseInput);
	IC->BindAxis("MoveRight", this, &AUTVehicle::OnSteeringInput);
	IC->BindAxis("MoveLeft", this, &AUTVehicle::OnSteerLeftInput);
	IC->BindAction("Jump", IE_Pressed, this, &AUTVehicle::HandleHandbrakePressed);
	IC->BindAction("Jump", IE_Released, this, &AUTVehicle::HandleHandbrakeReleased);
	// Observe physical press state without consuming it; AUTPlayerController's
	// lower component still owns held flags, input gating, and deferred dispatch.
	// Direct releases supply the non-character stop callback that UT omits.
	FInputActionBinding& PrimaryFirePressed = IC->BindAction(
		"StartFire", IE_Pressed, this, &AUTVehicle::HandlePrimaryFirePressed);
	PrimaryFirePressed.bConsumeInput = false;
	FInputActionBinding& PrimaryFireReleased = IC->BindAction(
		"StopFire", IE_Released, this, &AUTVehicle::HandlePrimaryFireReleased);
	PrimaryFireReleased.bConsumeInput = false;
	FInputActionBinding& AltFirePressed = IC->BindAction(
		"StartAltFire", IE_Pressed, this, &AUTVehicle::HandleAltFirePressed);
	AltFirePressed.bConsumeInput = false;
	FInputActionBinding& AltFireReleased = IC->BindAction(
		"StopAltFire", IE_Released, this, &AUTVehicle::HandleAltFireReleased);
	AltFireReleased.bConsumeInput = false;
	FInputKeyBinding& HornBinding = IC->BindKey(EKeys::H, IE_Pressed, this, &AUTVehicle::OnHornPressed);
	HornBinding.bConsumeInput = true;
	// Match UT3's use flow: the same stock, remappable ActivateSpecial action
	// enters nearby vehicles and exits the currently possessed vehicle.
	FInputActionBinding& ExitBinding = IC->BindAction(
		"ActivateSpecial", IE_Pressed, this, &AUTVehicle::ServerDriverLeave);
	ExitBinding.bConsumeInput = true;
	BindVehicleSpecificInput(IC);
	IC->RegisterComponent();
	PC->PushInputComponent(IC);

	int32 ForwardMappings = 0;
	int32 BackwardMappings = 0;
	int32 LeftMappings = 0;
	int32 RightMappings = 0;
	for (const FInputAxisKeyMapping& Mapping : PC->PlayerInput->AxisMappings)
	{
		ForwardMappings += Mapping.AxisName == FName(TEXT("MoveForward")) ? 1 : 0;
		BackwardMappings += Mapping.AxisName == FName(TEXT("MoveBackward")) ? 1 : 0;
		LeftMappings += Mapping.AxisName == FName(TEXT("MoveLeft")) ? 1 : 0;
		RightMappings += Mapping.AxisName == FName(TEXT("MoveRight")) ? 1 : 0;
	}
	UE_LOG(LogTemp, Warning, TEXT("[VehicleInput] Capture pushed PC=%s Component=%s Priority=%d AxisMappings F=%d B=%d L=%d R=%d"),
		*PC->GetName(), *IC->GetName(), IC->Priority, ForwardMappings, BackwardMappings, LeftMappings, RightMappings);
}

void AUTVehicle::UnbindDrivingInput()
{
	// Continuous vehicle abilities must receive a release even when possession,
	// controller replication, or a rebind removes the capture while a key is held.
	HandleHandbrakeReleased();
	HandlePrimaryFireReleased();
	HandleAltFireReleased();

	if (BoundInputPC != nullptr && DrivingInputComponent != nullptr)
	{
		BoundInputPC->PopInputComponent(DrivingInputComponent);
		UE_LOG(LogTemp, Warning, TEXT("[VehicleInput] Capture popped PC=%s Component=%s"),
			*BoundInputPC->GetName(), *DrivingInputComponent->GetName());
	}
	if (DrivingInputComponent != nullptr)
	{
		DrivingInputComponent->DestroyComponent();
		DrivingInputComponent = nullptr;
	}
	BoundInputPC = nullptr;

	// Zero the sim inputs so an empty vehicle doesn't keep driving
	ThrottleAxisValue = 0.0f;
	ReverseAxisValue = 0.0f;
	SteerRightAxisValue = 0.0f;
	SteerLeftAxisValue = 0.0f;
	ApplyDriveInput();
}

void AUTVehicle::UnPossessed()
{
	DeactivateVehicleCamera();
	UnbindDrivingInput();
	Super::UnPossessed();
}

void AUTVehicle::OnRep_Controller()
{
	Super::OnRep_Controller();
	UE_LOG(LogTemp, Warning, TEXT("[VehicleInput] OnRep_Controller Vehicle=%s Role=%d Controller=%s Local=%d"),
		*GetName(), (int32)Role, *GetNameSafe(Controller), IsLocallyControlled() ? 1 : 0);
	if (Controller == nullptr)
	{
		DeactivateVehicleCamera();
		UnbindDrivingInput();
	}
	else if (IsLocallyControlled())
	{
		// ClientRestart normally performs the bind via PawnClientRestart. This
		// also covers possession paths where controller replication arrives
		// without that callback ordering.
		ActivateVehicleCamera();
		BindDrivingInput();
	}
}

void AUTVehicle::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DeactivateVehicleCamera();
	UnbindDrivingInput();
	Super::EndPlay(EndPlayReason);
}

void AUTVehicle::ApplyDriveInput()
{
	UWheeledVehicleMovementComponent* Movement = GetVehicleMovementComponent();
	if (Movement == nullptr)
	{
		return;
	}

	// This matches the working BP exactly: W/S are combined into one signed
	// throttle value. PhysX's bReverseAsBrake logic consumes negative throttle;
	// converting S into SetBrakeInput prevents its automatic reverse shift.
	const float Throttle = FMath::Clamp(ThrottleAxisValue - ReverseAxisValue, -1.0f, 1.0f);
	const float Steering = SteerRightAxisValue - SteerLeftAxisValue;
	if ((FMath::Abs(Throttle) > 0.01f || FMath::Abs(Steering) > 0.01f) && GetMesh() != nullptr)
	{
		// Input may arrive after an unattended vehicle has gone to sleep. Wake the
		// chassis before handing the values to PhysX so the first press is applied.
		GetMesh()->WakeAllRigidBodies();
	}
	if (FMath::Abs(Throttle) > 0.01f)
	{
		// This vehicle has one forward gear. Select the requested direction now
		// instead of spending the first input frames in PhysX neutral/autobox delay.
		Movement->SetUseAutoGears(false);
		Movement->SetTargetGear(Throttle > 0.0f ? 1 : -1, true);
	}

	bVacantBrakeApplied = false;
	Movement->SetThrottleInput(Throttle);
	Movement->SetBrakeInput(0.0f);
	Movement->SetSteeringInput(Steering);
	Movement->SetHandbrakeInput(bHandbrakeInputDown);

	const float Now = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0f;
	if ((FMath::Abs(Throttle) > 0.01f || FMath::Abs(Steering) > 0.01f) && Now >= NextDriveInputLogTime)
	{
		NextDriveInputLogTime = Now + 0.5f;
		FBodyInstance* ChassisBody = GetMesh() != nullptr ? GetMesh()->GetBodyInstance() : nullptr;
		UE_LOG(LogTemp, Warning, TEXT("[VehicleInput] Applied Movement=%s SignedThrottle=%.3f Steering=%.3f Role=%d Local=%d Physics=%d MeshSim=%d Chassis=%d Speed=%.1f RPM=%.1f Gear=%d/%d Wheels=%d"),
			*Movement->GetName(), Throttle, Steering,
			(int32)Role, IsLocallyControlled() ? 1 : 0,
			Movement->HasValidPhysicsState() ? 1 : 0,
			GetMesh() != nullptr && GetMesh()->IsSimulatingPhysics() ? 1 : 0,
			ChassisBody != nullptr && ChassisBody->IsValidBodyInstance() ? 1 : 0,
			Movement->GetForwardSpeed(), Movement->GetEngineRotationSpeed(),
			Movement->GetCurrentGear(), Movement->GetTargetGear(), Movement->Wheels.Num());

		for (int32 WheelIndex = 0; WheelIndex < Movement->Wheels.Num(); ++WheelIndex)
		{
			UVehicleWheel* Wheel = Movement->Wheels[WheelIndex];
			if (Wheel != nullptr)
			{
				FHitResult GroundHit;
				FCollisionQueryParams GroundQuery(FName(TEXT("VehicleWheelGround")), false, this);
				const FVector GroundTraceStart = Wheel->Location + FVector(0.0f, 0.0f, 200.0f);
				const FVector GroundTraceEnd = Wheel->Location - FVector(0.0f, 0.0f, 500.0f);
				const bool bGroundHit = GetWorld() != nullptr && GetWorld()->LineTraceSingleByChannel(
					GroundHit, GroundTraceStart, GroundTraceEnd, ECC_Vehicle, GroundQuery);
				const float GroundZ = bGroundHit ? GroundHit.ImpactPoint.Z : 0.0f;

				UE_LOG(LogTemp, Warning, TEXT("[VehicleWheel] I=%d Bone=%s Contact=%d Loc=%s Susp=%.2f Load=%.2f NormLoad=%.3f Torque=%.2f LongForce=%.2f LongSlip=%.3f Ground=%d GroundZ=%.2f CenterGap=%.2f Hit=%s/%s"),
					WheelIndex,
					Movement->WheelSetups.IsValidIndex(WheelIndex) ? *Movement->WheelSetups[WheelIndex].BoneName.ToString() : TEXT("NONE"),
					Wheel->GetContactSurfaceMaterial() != nullptr ? 1 : 0,
					*Wheel->Location.ToString(), Wheel->GetSuspensionOffset(),
					Wheel->DebugTireLoad, Wheel->DebugNormalizedTireLoad,
					Wheel->DebugWheelTorque, Wheel->DebugLongForce, Wheel->DebugLongSlip,
					bGroundHit ? 1 : 0, GroundZ, bGroundHit ? Wheel->Location.Z - GroundZ : 0.0f,
					bGroundHit ? *GetNameSafe(GroundHit.GetActor()) : TEXT("NONE"),
					bGroundHit ? *GetNameSafe(GroundHit.GetComponent()) : TEXT("NONE"));
			}
		}
	}
}

bool AUTVehicle::ShouldApplyVacantBrake() const
{
	return VehicleComponent != nullptr && !VehicleComponent->HasDriver() && !VehicleComponent->bDead;
}

void AUTVehicle::UpdateVacantBrake()
{
	UWheeledVehicleMovementComponent* Movement = GetVehicleMovementComponent();
	if (Movement == nullptr)
	{
		return;
	}

	if (ShouldApplyVacantBrake())
	{
		// A normal exit must not leave the drivetrain freewheeling. Service braking
		// stops a moving vehicle; once nearly stopped, the handbrake holds it and
		// prevents unloaded driven wheels from spinning against an obstruction.
		Movement->SetThrottleInput(0.0f);
		Movement->SetSteeringInput(0.0f);
		Movement->SetBrakeInput(1.0f);
		const bool bNearlyStopped = GetVelocity().Size2D() < 100.0f &&
			FMath::Abs(Movement->GetForwardSpeed()) < 100.0f;
		Movement->SetHandbrakeInput(bNearlyStopped);
		bVacantBrakeApplied = true;
	}
	else if (bVacantBrakeApplied)
	{
		// Driver entry (or an armed Scorpion eject) releases only the automatic
		// brake. A driver's own handbrake state remains authoritative.
		Movement->SetBrakeInput(0.0f);
		Movement->SetHandbrakeInput(bHandbrakeInputDown);
		bVacantBrakeApplied = false;
	}
}

void AUTVehicle::OnThrottleInput(float Value)
{
	ThrottleAxisValue = Value;
	ApplyDriveInput();
}

void AUTVehicle::OnReverseInput(float Value)
{
	ReverseAxisValue = Value;
	ApplyDriveInput();
}

void AUTVehicle::OnSteeringInput(float Value)
{
	SteerRightAxisValue = Value;
	ApplyDriveInput();
}

void AUTVehicle::OnSteerLeftInput(float Value)
{
	SteerLeftAxisValue = Value;
	ApplyDriveInput();
}

void AUTVehicle::OnHandbrakePressed()
{
	UWheeledVehicleMovementComponent* Movement = GetVehicleMovementComponent();
	if (Movement != nullptr)
	{
		Movement->SetHandbrakeInput(true);
	}
}

void AUTVehicle::OnHandbrakeReleased()
{
	UWheeledVehicleMovementComponent* Movement = GetVehicleMovementComponent();
	if (Movement != nullptr)
	{
		Movement->SetHandbrakeInput(false);
	}
}

void AUTVehicle::OnPrimaryFirePressed()
{
}

void AUTVehicle::OnPrimaryFireReleased()
{
}

void AUTVehicle::OnAltFirePressed()
{
}

void AUTVehicle::OnAltFireReleased()
{
}

void AUTVehicle::HandleHandbrakePressed()
{
	if (!bHandbrakeInputDown)
	{
		bHandbrakeInputDown = true;
		OnHandbrakePressed();
	}
}

void AUTVehicle::HandleHandbrakeReleased()
{
	if (bHandbrakeInputDown)
	{
		bHandbrakeInputDown = false;
		OnHandbrakeReleased();
	}
}

void AUTVehicle::HandlePrimaryFirePressed()
{
	bPrimaryFireKeyDown = true;
}

void AUTVehicle::HandlePrimaryFireReleased()
{
	bPrimaryFireKeyDown = false;
	StopVehicleFire(0);
}

void AUTVehicle::HandleAltFirePressed()
{
	bAltFireKeyDown = true;
}

void AUTVehicle::HandleAltFireReleased()
{
	bAltFireKeyDown = false;
	StopVehicleFire(1);
}

void AUTVehicle::BindVehicleSpecificInput(UInputComponent*)
{
}

void AUTVehicle::PawnStartFire(uint8 FireModeNum)
{
	// AUTPlayerController forwards deferred press events to any pawn, but only
	// forwards releases to AUTCharacter. Physical-key state rejects a deferred
	// press if that key was already released in the same input frame.
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

void AUTVehicle::StopVehicleFire(uint8 FireModeNum)
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

void AUTVehicle::OnHornPressed()
{
	if (VehicleComponent != nullptr)
	{
		VehicleComponent->RequestHorn();
	}
}

float AUTVehicle::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
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

void AUTVehicle::PawnClientRestart()
{
	Super::PawnClientRestart();
	UE_LOG(LogTemp, Warning, TEXT("[VehicleInput] PawnClientRestart Vehicle=%s Role=%d Controller=%s Local=%d PawnInput=%s"),
		*GetName(), (int32)Role, *GetNameSafe(Controller), IsLocallyControlled() ? 1 : 0, *GetNameSafe(InputComponent));

	// Ensure the camera is active
	if (Camera != nullptr)
	{
		Camera->Activate();
	}
	ActivateVehicleCamera();

	// Runs on the owning client (and listen host) after possession.
	BindDrivingInput();
}

void AUTVehicle::PostRender(AUTHUD* HUD, UCanvas* Canvas)
{
	if (VehicleComponent != nullptr && Controller != nullptr)
	{
		APlayerController* PC = Cast<APlayerController>(Controller);
		VehicleComponent->DrawVehicleHUD(HUD, Canvas, PC);
	}
}

void AUTVehicle::PostRenderFor(APlayerController* PC, UCanvas* Canvas, FVector CameraPosition, FVector CameraDir)
{
	Super::PostRenderFor(PC, Canvas, CameraPosition, CameraDir);
	if (VehicleComponent != nullptr && PC != nullptr)
	{
		VehicleComponent->DrawEntryPrompt(Cast<AUTHUD>(PC->MyHUD), Canvas, PC);
	}
}

bool AUTVehicle::ServerDriverLeave_Validate()
{
	return true;
}

void AUTVehicle::ServerDriverLeave_Implementation()
{
	HandleDriverLeaveRequest();
}

bool AUTVehicle::HandleDriverLeaveRequest()
{
	return VehicleComponent != nullptr && VehicleComponent->DriverLeave(false);
}
