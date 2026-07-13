#include "UTVehicleMovementHover.h"
#include "UnrealTournament.h"
#include "UnrealNetwork.h"

UUTVehicleMovementHover::UUTVehicleMovementHover()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicated(true);

	MaxSpeed = 2200.0f;
	MaxAltitude = 4000.0f;
	ThrustForce = 3000.0f;
	LiftForce = 2500.0f;
	StrafeForce = 1500.0f;
	GravityForce = 980.0f;
	TurnRate = 120.0f;
	PitchRate = 60.0f;
	VelocityDamping = 0.02f;
	MinHoverHeight = 100.0f;

	RawThrottleInput = 0.0f;
	RawSteeringInput = 0.0f;
	RawLiftInput = 0.0f;
	RawPitchInput = 0.0f;
}

void UUTVehicleMovementHover::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UUTVehicleMovementHover, ReplicatedState);
}

void UUTVehicleMovementHover::SetThrottleInput(float Value)
{
	RawThrottleInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void UUTVehicleMovementHover::SetSteeringInput(float Value)
{
	RawSteeringInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void UUTVehicleMovementHover::SetLiftInput(float Value)
{
	RawLiftInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void UUTVehicleMovementHover::SetPitchInput(float Value)
{
	RawPitchInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

float UUTVehicleMovementHover::GetCurrentSpeed() const
{
	return Velocity.Size();
}

float UUTVehicleMovementHover::GetGroundDistance() const
{
	APawn* Owner = Cast<APawn>(GetOwner());
	if (Owner == nullptr)
	{
		return MaxAltitude;
	}

	FVector Start = Owner->GetActorLocation();
	FVector End = Start - FVector(0.0f, 0.0f, MaxAltitude + 1000.0f);

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Owner);

	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		return Hit.Distance;
	}

	return MaxAltitude;
}

void UUTVehicleMovementHover::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APawn* Owner = Cast<APawn>(GetOwner());
	if (Owner == nullptr)
	{
		return;
	}

	// On owning client: send inputs to server
	if (Owner->IsLocallyControlled() && Owner->Role < ROLE_Authority)
	{
		ServerUpdateState(RawThrottleInput, RawSteeringInput, RawLiftInput, RawPitchInput);
	}

	// On server or standalone: apply physics
	if (Owner->Role == ROLE_Authority)
	{
		UpdateFlightPhysics(DeltaTime);

		// Update replicated state
		ReplicatedState.ThrottleInput = RawThrottleInput;
		ReplicatedState.SteeringInput = RawSteeringInput;
		ReplicatedState.LiftInput = RawLiftInput;
		ReplicatedState.PitchInput = RawPitchInput;
		ReplicatedState.Rotation = Owner->GetActorRotation();
	}
	else if (!Owner->IsLocallyControlled())
	{
		// Remote clients: use replicated state for visual interpolation
		// Position comes from standard AActor replication (ReplicatedMovement)
		// We can smooth the rotation here
		FRotator CurrentRot = Owner->GetActorRotation();
		FRotator TargetRot = ReplicatedState.Rotation;
		FRotator SmoothedRot = FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, 10.0f);
		Owner->SetActorRotation(SmoothedRot);
	}
	else
	{
		// Locally controlled on authority (listen server host): apply physics directly
		UpdateFlightPhysics(DeltaTime);
	}
}

void UUTVehicleMovementHover::UpdateFlightPhysics(float DeltaTime)
{
	APawn* Owner = Cast<APawn>(GetOwner());
	if (Owner == nullptr)
	{
		return;
	}

	FRotator CurrentRotation = Owner->GetActorRotation();

	// Apply yaw from steering input
	float YawDelta = RawSteeringInput * TurnRate * DeltaTime;
	CurrentRotation.Yaw += YawDelta;

	// Apply pitch from mouse look (clamped)
	float PitchDelta = RawPitchInput * PitchRate * DeltaTime;
	CurrentRotation.Pitch = FMath::Clamp(CurrentRotation.Pitch + PitchDelta, -60.0f, 60.0f);

	// Roll follows steering for visual feel
	float TargetRoll = -RawSteeringInput * 25.0f;
	CurrentRotation.Roll = FMath::FInterpTo(CurrentRotation.Roll, TargetRoll, DeltaTime, 5.0f);

	Owner->SetActorRotation(CurrentRotation);

	// Calculate thrust direction from forward vector
	FVector ForwardDir = CurrentRotation.Vector();
	FVector RightDir = FRotationMatrix(CurrentRotation).GetScaledAxis(EAxis::Y);
	FVector UpDir = FVector::UpVector;

	// Build acceleration from inputs
	FVector Acceleration = FVector::ZeroVector;

	// Forward/backward thrust
	Acceleration += ForwardDir * RawThrottleInput * ThrustForce;

	// Vertical lift
	Acceleration += UpDir * RawLiftInput * LiftForce;

	// Gravity (always pulling down when not lifting)
	if (RawLiftInput <= 0.0f)
	{
		Acceleration -= UpDir * GravityForce;
	}
	else
	{
		// Reduced gravity when actively lifting
		Acceleration -= UpDir * GravityForce * 0.3f;
	}

	// Ground repulsion - hover effect
	float GroundDist = GetGroundDistance();
	if (GroundDist < MinHoverHeight)
	{
		float RepulsionStrength = (1.0f - (GroundDist / MinHoverHeight)) * LiftForce * 2.0f;
		Acceleration += UpDir * RepulsionStrength;
	}

	// Apply acceleration to velocity
	Velocity += Acceleration * DeltaTime;

	// Apply damping
	Velocity *= (1.0f - VelocityDamping);

	// Clamp speed
	float CurrentSpeed = Velocity.Size();
	if (CurrentSpeed > MaxSpeed)
	{
		Velocity = Velocity.GetSafeNormal() * MaxSpeed;
	}

	// Altitude ceiling
	FVector NewLocation = Owner->GetActorLocation() + Velocity * DeltaTime;

	// Check altitude relative to ground
	if (GroundDist > MaxAltitude && Velocity.Z > 0.0f)
	{
		Velocity.Z = 0.0f;
		NewLocation.Z = Owner->GetActorLocation().Z;
	}

	// Don't go below ground
	if (NewLocation.Z < Owner->GetActorLocation().Z - GroundDist + 50.0f)
	{
		NewLocation.Z = Owner->GetActorLocation().Z - GroundDist + 50.0f;
		if (Velocity.Z < 0.0f)
		{
			Velocity.Z = 0.0f;
		}
	}

	// Sweep move
	FHitResult Hit;
	SafeMoveUpdatedComponent(NewLocation - Owner->GetActorLocation(), CurrentRotation, true, Hit);

	if (Hit.IsValidBlockingHit())
	{
		// Slide along surfaces
		SlideAlongSurface(Velocity * DeltaTime, 1.0f - Hit.Time, Hit.Normal, Hit);
		Velocity = Velocity - (Hit.Normal * FVector::DotProduct(Velocity, Hit.Normal));
	}
}

bool UUTVehicleMovementHover::ServerUpdateState_Validate(float InThrottle, float InSteering, float InLift, float InPitch)
{
	return true;
}

void UUTVehicleMovementHover::ServerUpdateState_Implementation(float InThrottle, float InSteering, float InLift, float InPitch)
{
	RawThrottleInput = FMath::Clamp(InThrottle, -1.0f, 1.0f);
	RawSteeringInput = FMath::Clamp(InSteering, -1.0f, 1.0f);
	RawLiftInput = FMath::Clamp(InLift, -1.0f, 1.0f);
	RawPitchInput = FMath::Clamp(InPitch, -1.0f, 1.0f);
}
