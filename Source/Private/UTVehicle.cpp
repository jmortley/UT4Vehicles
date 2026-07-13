#include "UTVehicle.h"
#include "UnrealTournament.h"
#include "WheeledVehicleMovementComponent.h"
#include "UTCharacter.h"
#include "UTPlayerController.h"
#include "UTHUD.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"

AUTVehicle::AUTVehicle(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	VehicleComponent = CreateDefaultSubobject<UUTVehicleComponent>(TEXT("VehicleComponent"));

	// Camera setup
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(GetMesh());
	SpringArm->TargetArmLength = 600.0f;
	SpringArm->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
	SpringArm->bUsePawnControlRotation = true;
	SpringArm->bInheritPitch = true;
	SpringArm->bInheritYaw = true;
	SpringArm->bInheritRoll = false;
	SpringArm->bDoCollisionTest = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);

	SetReplicates(true);
	bAlwaysRelevant = true;
}

void AUTVehicle::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// These must be set after the BodyInstance is fully initialized
	GetMesh()->SetAngularDamping(2.0f);
	GetMesh()->SetLinearDamping(0.1f);
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
	UE_LOG(LogTemp, Warning, TEXT("[Vehicle] SetupPlayerInputComponent called! Controller=%s"), Controller ? *Controller->GetName() : TEXT("NONE"));

	PlayerInputComponent->BindAxis("MoveForward", this, &AUTVehicle::OnThrottleInput);
	PlayerInputComponent->BindAxis("MoveRight", this, &AUTVehicle::OnSteeringInput);
	PlayerInputComponent->BindAxis("MoveBackward", this, &AUTVehicle::OnBrakeInput);

	PlayerInputComponent->BindAction("Use", IE_Pressed, this, &AUTVehicle::ServerDriverLeave);
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &AUTVehicle::OnHandbrakePressed);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &AUTVehicle::OnHandbrakeReleased);
}

void AUTVehicle::OnThrottleInput(float Value)
{
	if (FMath::Abs(Value) > 0.01f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Vehicle] OnThrottleInput: %f"), Value);
	}
	UWheeledVehicleMovementComponent* Movement = GetVehicleMovementComponent();
	if (Movement != nullptr)
	{
		if (Value > 0.0f)
		{
			Movement->SetThrottleInput(Value);
			Movement->SetBrakeInput(0.0f);
		}
		else if (Value < 0.0f)
		{
			Movement->SetThrottleInput(0.0f);
			Movement->SetBrakeInput(-Value);
		}
		else
		{
			Movement->SetThrottleInput(0.0f);
			Movement->SetBrakeInput(0.0f);
		}
	}
}

void AUTVehicle::OnSteeringInput(float Value)
{
	UWheeledVehicleMovementComponent* Movement = GetVehicleMovementComponent();
	if (Movement != nullptr)
	{
		Movement->SetSteeringInput(Value);
	}
}

void AUTVehicle::OnBrakeInput(float Value)
{
	// Handled in OnThrottleInput via negative MoveForward
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

float AUTVehicle::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (VehicleComponent != nullptr)
	{
		return VehicleComponent->ApplyDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
	}
	return 0.0f;
}

void AUTVehicle::PawnClientRestart()
{
	Super::PawnClientRestart();

	// Ensure the camera is active
	if (Camera != nullptr)
	{
		Camera->Activate();
	}
}

void AUTVehicle::PostRender(AUTHUD* HUD, UCanvas* Canvas)
{
	if (VehicleComponent != nullptr && Controller != nullptr)
	{
		APlayerController* PC = Cast<APlayerController>(Controller);
		VehicleComponent->DrawVehicleHUD(HUD, Canvas, PC);
	}
}

bool AUTVehicle::ServerTryEnter_Validate(APawn* NewDriver)
{
	return true;
}

void AUTVehicle::ServerTryEnter_Implementation(APawn* NewDriver)
{
	if (VehicleComponent != nullptr)
	{
		VehicleComponent->TryToDrive(NewDriver);
	}
}

bool AUTVehicle::ServerDriverLeave_Validate()
{
	return true;
}

void AUTVehicle::ServerDriverLeave_Implementation()
{
	if (VehicleComponent != nullptr)
	{
		VehicleComponent->DriverLeave(false);
	}
}
