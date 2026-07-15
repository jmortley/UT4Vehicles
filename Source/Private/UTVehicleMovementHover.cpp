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
	ReverseThrustForce = 1500.0f;
	LiftForce = 2500.0f;
	StrafeForce = 1500.0f;
	GravityForce = 980.0f;
	TurnRate = 120.0f;
	PitchRate = 60.0f;
	VelocityDamping = 0.7f;
	StopThreshold = 100.0f;
	MinHoverHeight = 100.0f;
	HoverVerticalDamping = 6.0f;
	ParkedGroundClearance = 60.0f;
	ParkedDescentSpeed = 400.0f;
	ParkedHorizontalDeceleration = 3500.0f;
	ParkedGroundSearchDistance = 20000.0f;

	RawThrottleInput = 0.0f;
	RawSteeringInput = 0.0f;
	RawLiftInput = 0.0f;
	RawPitchInput = 0.0f;
	bParkingMode = true;
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

void UUTVehicleMovementHover::SetParkingMode(bool bEnabled)
{
	if (bParkingMode == bEnabled)
	{
		return;
	}

	bParkingMode = bEnabled;
	if (bParkingMode)
	{
		RawThrottleInput = 0.0f;
		RawSteeringInput = 0.0f;
		RawLiftInput = 0.0f;
		RawPitchInput = 0.0f;
	}

	UE_LOG(LogTemp, Warning, TEXT("[VehicleHoverParking] Vehicle=%s Enabled=%d Speed=%.1f GroundDistance=%.1f"),
		*GetNameSafe(GetOwner()), bParkingMode ? 1 : 0, Velocity.Size(), GetGroundDistance());
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
	const float TraceDistance = FMath::Max(MaxAltitude + 1000.0f, ParkedGroundSearchDistance);
	FVector End = Start - FVector(0.0f, 0.0f, TraceDistance);

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
		if (bParkingMode || Owner->Controller == nullptr)
		{
			UpdateParkedPhysics(DeltaTime);
		}
		else
		{
			UpdateFlightPhysics(DeltaTime);
		}

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
		// Preserve local prediction for the owning client while the server runs
		// the authoritative copy from the submitted input state.
		UpdateFlightPhysics(DeltaTime);
	}
}

void UUTVehicleMovementHover::UpdateParkedPhysics(float DeltaTime)
{
	APawn* Owner = Cast<APawn>(GetOwner());
	if (Owner == nullptr || UpdatedComponent == nullptr)
	{
		return;
	}

	// UT3 flyers return to their landing gear after the driver exits. Brake the
	// inherited planar velocity hard enough to stay near the exit point, but do
	// not teleport to a stop; vertical motion is separately capped below.
	RawThrottleInput = 0.0f;
	RawSteeringInput = 0.0f;
	RawLiftInput = 0.0f;
	RawPitchInput = 0.0f;
	FVector PlanarVelocity(Velocity.X, Velocity.Y, 0.0f);
	PlanarVelocity = FMath::VInterpConstantTo(PlanarVelocity, FVector::ZeroVector,
		DeltaTime, FMath::Max(ParkedHorizontalDeceleration, 0.0f));
	Velocity.X = PlanarVelocity.X;
	Velocity.Y = PlanarVelocity.Y;

	const float GroundDistance = GetGroundDistance();
	const float VerticalCorrection = ParkedGroundClearance - GroundDistance;
	const float MaxStep = FMath::Max(ParkedDescentSpeed, 0.0f) * DeltaTime;
	const float MoveZ = FMath::Clamp(VerticalCorrection, -MaxStep, MaxStep);
	Velocity.Z = DeltaTime > SMALL_NUMBER ? MoveZ / DeltaTime : 0.0f;

	FRotator ParkedRotation = Owner->GetActorRotation();
	ParkedRotation.Pitch = FMath::FInterpTo(ParkedRotation.Pitch, 0.0f, DeltaTime, 4.0f);
	ParkedRotation.Roll = FMath::FInterpTo(ParkedRotation.Roll, 0.0f, DeltaTime, 4.0f);

	FHitResult Hit;
	SafeMoveUpdatedComponent(FVector(Velocity.X * DeltaTime, Velocity.Y * DeltaTime, MoveZ),
		ParkedRotation, true, Hit);
	if (Hit.IsValidBlockingHit())
	{
		Velocity -= Hit.Normal * FVector::DotProduct(Velocity, Hit.Normal);
	}
	if (FMath::Abs(VerticalCorrection) <= 2.0f && PlanarVelocity.SizeSquared() < FMath::Square(StopThreshold))
	{
		Velocity = FVector::ZeroVector;
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

	// Mouse/controller view supplies heading and aim. Movement keys never alter
	// altitude: W/S use a yaw-only forward vector and A/D use its right vector.
	const FRotator ViewRotation = Owner->Controller != nullptr
		? Owner->Controller->GetControlRotation()
		: CurrentRotation;
	const float ViewPitch = FMath::Clamp(FRotator::NormalizeAxis(ViewRotation.Pitch), -45.0f, 45.0f);
	CurrentRotation.Yaw = FMath::FixedTurn(CurrentRotation.Yaw, ViewRotation.Yaw, TurnRate * DeltaTime);
	CurrentRotation.Pitch = FMath::FInterpTo(CurrentRotation.Pitch, ViewPitch, DeltaTime, 4.0f);

	// Roll follows steering for visual feel
	float TargetRoll = -RawSteeringInput * 25.0f;
	CurrentRotation.Roll = FMath::FInterpTo(CurrentRotation.Roll, TargetRoll, DeltaTime, 5.0f);

	Owner->SetActorRotation(CurrentRotation);

	// Planar directions are camera-relative but deliberately ignore camera pitch.
	const FRotator PlanarView(0.0f, ViewRotation.Yaw, 0.0f);
	FVector ForwardDir = PlanarView.Vector();
	FVector RightDir = FRotationMatrix(PlanarView).GetScaledAxis(EAxis::Y);
	FVector UpDir = FVector::UpVector;

	// Build acceleration from inputs
	FVector Acceleration = FVector::ZeroVector;

	// UT3's chopper simulation has separate forward/reverse forces. In
	// particular, the Raptor uses only 100/750 of its forward force in reverse.
	const float LongitudinalForce = RawThrottleInput >= 0.0f ? ThrustForce : ReverseThrustForce;
	Acceleration += ForwardDir * RawThrottleInput * LongitudinalForce;

	// Left/right strafe. Steering input is not yaw for UT3-style Raptor flight.
	Acceleration += RightDir * RawSteeringInput * StrafeForce;

	// UT3's Raptor holds altitude like a Harrier when neither vertical control
	// is pressed. Space applies lift and Crouch applies descent; neutral input
	// arrests only vertical inertia without disturbing planar flight.
	const bool bAscending = RawLiftInput > KINDA_SMALL_NUMBER;
	const bool bDescending = RawLiftInput < -KINDA_SMALL_NUMBER;
	if (bAscending)
	{
		Acceleration += UpDir * (RawLiftInput * LiftForce - GravityForce * 0.3f);
	}
	else if (bDescending)
	{
		Acceleration += UpDir * RawLiftInput * LiftForce;
	}
	else
	{
		Velocity.Z = FMath::FInterpTo(Velocity.Z, 0.0f, DeltaTime, HoverVerticalDamping);
	}

	// Ground repulsion - hover effect
	float GroundDist = GetGroundDistance();
	if (GroundDist < MinHoverHeight)
	{
		float RepulsionStrength = (1.0f - (GroundDist / MinHoverHeight)) * LiftForce * 2.0f;
		RepulsionStrength -= Velocity.Z * HoverVerticalDamping;
		Acceleration += UpDir * RepulsionStrength;
	}

	// Apply acceleration to velocity
	Velocity += Acceleration * DeltaTime;

	// UT3's Long/Lat/Up damping values are continuous coefficients, not a fixed
	// percentage removed once per frame. Exponential damping preserves the same
	// response at 30, 60, and 120 Hz.
	Velocity *= FMath::Exp(-FMath::Max(VelocityDamping, 0.0f) * DeltaTime);
	if (FMath::IsNearlyZero(RawThrottleInput) && FMath::IsNearlyZero(RawSteeringInput) &&
		FMath::IsNearlyZero(RawLiftInput) && Velocity.SizeSquared() < FMath::Square(StopThreshold))
	{
		Velocity = FVector::ZeroVector;
	}

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
