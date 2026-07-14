#include "UTVehicleFlying.h"
#include "UnrealTournament.h"
#include "UTCharacter.h"
#include "UTPlayerController.h"
#include "UTHUD.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"

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

	bLiftUp = false;
	bLiftDown = false;
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

	// UT3-style flight controls:
	// W/S = forward/backward thrust
	// A/D = yaw (steering)
	// Space = ascend
	// Ctrl/Crouch = descend
	// Mouse Y = pitch

	PlayerInputComponent->BindAxis("MoveForward", this, &AUTVehicleFlying::OnThrottleInput);
	PlayerInputComponent->BindAxis("MoveRight", this, &AUTVehicleFlying::OnSteeringInput);
	PlayerInputComponent->BindAxis("LookUp", this, &AUTVehicleFlying::OnMousePitch);

	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &AUTVehicleFlying::OnLiftUp);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &AUTVehicleFlying::OnLiftUpRelease);
	PlayerInputComponent->BindAction("Crouch", IE_Pressed, this, &AUTVehicleFlying::OnLiftDown);
	PlayerInputComponent->BindAction("Crouch", IE_Released, this, &AUTVehicleFlying::OnLiftDownRelease);

	PlayerInputComponent->BindAction("ActivateSpecial", IE_Pressed, this, &AUTVehicleFlying::ServerDriverLeave);
}

void AUTVehicleFlying::OnThrottleInput(float Value)
{
	if (HoverMovement != nullptr)
	{
		HoverMovement->SetThrottleInput(Value);
	}
}

void AUTVehicleFlying::OnSteeringInput(float Value)
{
	if (HoverMovement != nullptr)
	{
		HoverMovement->SetSteeringInput(Value);
	}
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

void AUTVehicleFlying::OnMousePitch(float Value)
{
	if (HoverMovement != nullptr)
	{
		HoverMovement->SetPitchInput(Value);
	}
}

float AUTVehicleFlying::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (VehicleComponent != nullptr)
	{
		return VehicleComponent->ApplyDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
	}
	return 0.0f;
}

void AUTVehicleFlying::PawnClientRestart()
{
	Super::PawnClientRestart();
	if (Camera != nullptr)
	{
		Camera->Activate();
	}
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

bool AUTVehicleFlying::ServerTryEnter_Validate(APawn* NewDriver)
{
	return true;
}

void AUTVehicleFlying::ServerTryEnter_Implementation(APawn* NewDriver)
{
	if (VehicleComponent != nullptr && NewDriver != nullptr &&
		NewDriver->Controller != nullptr && GetOwner() == NewDriver->Controller)
	{
		VehicleComponent->TryToDrive(NewDriver);
	}
}

bool AUTVehicleFlying::ServerDriverLeave_Validate()
{
	return true;
}

void AUTVehicleFlying::ServerDriverLeave_Implementation()
{
	if (VehicleComponent != nullptr)
	{
		VehicleComponent->DriverLeave(false);
	}
}
