#include "UTVehicle_Scorpion.h"
#include "UnrealTournament.h"
#include "WheeledVehicleMovementComponent4W.h"
#include "UTVehicleComponent.h"
#include "UTVehicleDamageType.h"
#include "UTVehicleMeshComponent.h"
#include "UTGameplayStatics.h"
#include "UnrealNetwork.h"
#include "PhysicsEngine/BodySetup.h"
#include "TireConfig.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"

namespace
{
	void ConfigureScorpionDriveFromWorkingBP(UWheeledVehicleMovementComponent4W* Movement4W, USkeletalMeshComponent* Mesh)
	{
		if (UUTVehicleMeshComponent* VehicleMesh = Cast<UUTVehicleMeshComponent>(Mesh))
		{
			// The imported tire is about 65 uu from hub to tread while the PhysX
			// wheel shape is 45 uu. Match the visible hub to the physical contact
			// patch without changing suspension, gearing, or traction.
			VehicleMesh->VisualWheelCenterOffsetZ = 20.0f;
		}

		// The supplied folder has no runnable Scorpion BP. Copy the powertrain
		// defaults from working BP_GoliathTest_1, but keep wheel placement tied
		// to the Scorpion mesh's own tire bones.
		Movement4W->WheelSetups.SetNum(4);
		Movement4W->WheelSetups[0].WheelClass = UUTWheel_Scorpion_Front::StaticClass();
		Movement4W->WheelSetups[0].BoneName = FName(TEXT("F_L_Tire"));
		Movement4W->WheelSetups[1].WheelClass = UUTWheel_Scorpion_Front::StaticClass();
		Movement4W->WheelSetups[1].BoneName = FName(TEXT("F_R_Tire"));
		Movement4W->WheelSetups[2].WheelClass = UUTWheel_Scorpion_Rear::StaticClass();
		Movement4W->WheelSetups[2].BoneName = FName(TEXT("B_L_Tire"));
		Movement4W->WheelSetups[3].WheelClass = UUTWheel_Scorpion_Rear::StaticClass();
		Movement4W->WheelSetups[3].BoneName = FName(TEXT("B_R_Tire"));
		for (FWheelSetup& WheelSetup : Movement4W->WheelSetups)
		{
			WheelSetup.AdditionalOffset = FVector::ZeroVector;
		}

		// The supplied Scorpion tire is 2.0 by default, which still lets the
		// higher-output UT4-scale setup skate. This asset is Scorpion-specific,
		// so raise its combined longitudinal/lateral grip before PhysX rebuilds.
		UTireConfig* ScorpionTire = UUTWheel_Scorpion_Front::StaticClass()->GetDefaultObject<UUTWheel_Scorpion_Front>()->TireConfig;
		if (ScorpionTire != nullptr)
		{
			ScorpionTire->SetFrictionScale(3.5f);
		}

		// Working BP vehicles use mesh-specific wheel offsets. Contact alone is not
		// sufficient: the Scorpion's first corrected run contacted at -25 cm front
		// and -17 cm rear jounce, leaving the chassis collision to carry the weight
		// and the tires with no usable load. At 9 Hz, PhysX's static equilibrium is
		// gravity / frequency^2 = about +12 cm jounce. Lower the rest positions by
		// the measured delta so the suspension, rather than the chassis, supports
		// the vehicle. The first correction left the rear rest point 8 uu above the
		// front and produced a visible tail-down stance even though all four PhysX
		// wheel centers touched the floor correctly. Use one axle-center height so
		// the rear chassis comes up without changing radius or visual tire lift.
		if (Mesh != nullptr && Mesh->SkeletalMesh != nullptr)
		{
			const FVector MeshScale = Mesh->GetRelativeTransform().GetScale3D();
			for (FWheelSetup& WheelSetup : Movement4W->WheelSetups)
			{
				const FVector BonePosition = Mesh->SkeletalMesh->GetComposedRefPoseMatrix(WheelSetup.BoneName).GetOrigin() * MeshScale;
				const float TargetRestHeight = -24.5f;
				WheelSetup.AdditionalOffset.Z = TargetRestHeight - BonePosition.Z;
			}
		}

		Movement4W->EngineSetup.MaxRPM = 4000.0f;
		Movement4W->EngineSetup.MOI = 1.0f;
		Movement4W->EngineSetup.DampingRateFullThrottle = 0.05f;
		Movement4W->EngineSetup.DampingRateZeroThrottleClutchEngaged = 2.0f;
		Movement4W->EngineSetup.DampingRateZeroThrottleClutchDisengaged = 0.35f;
		FRichCurve* TorqueCurve = Movement4W->EngineSetup.TorqueCurve.GetRichCurve();
		TorqueCurve->Reset();
		// UT3's 950 uu/s cruise is slower than a UT4 dodge. Preserve the
		// Scorpion curve shape at UT4 scale: full pull through about 1900 uu/s,
		// taper at 2100, and a normal (non-boosted) top speed near 2300.
		TorqueCurve->AddKey(0.0f, 500.0f);
		TorqueCurve->AddKey(3225.5f, 500.0f);
		TorqueCurve->AddKey(3565.1f, 80.0f);
		TorqueCurve->AddKey(3904.6f, 0.0f);
		TorqueCurve->AddKey(4000.0f, 0.0f);

		Movement4W->TransmissionSetup.bUseGearAutoBox = true;
		Movement4W->TransmissionSetup.ClutchStrength = 10.0f;
		Movement4W->TransmissionSetup.NeutralGearUpRatio = 0.15f;
		Movement4W->TransmissionSetup.GearSwitchTime = 0.15f;
		Movement4W->TransmissionSetup.GearAutoBoxLatency = 2.0f;
		// 4:1 first gear x 2:1 final drive = 8:1 overall. A 45 cm wheel then
		// reaches about 2350 uu/s at the 4000 RPM limiter.
		Movement4W->TransmissionSetup.FinalRatio = 2.0f;
		Movement4W->TransmissionSetup.ReverseGearRatio = -4.0f;
		Movement4W->TransmissionSetup.ForwardGears.SetNum(1);
		Movement4W->TransmissionSetup.ForwardGears[0].Ratio = 4.0f;
		Movement4W->TransmissionSetup.ForwardGears[0].DownRatio = 0.5f;
		Movement4W->TransmissionSetup.ForwardGears[0].UpRatio = 0.5f;

		// The open diff was feeding 65% of the torque to the lightly loaded front
		// axle. One spinning front tire then drove engine RPM into the torque
		// cutoff while the chassis was still below 1000 uu/s. A tight, rear-biased
		// limited-slip setup keeps all four tire speeds useful on ramps and turns.
		Movement4W->DifferentialSetup.DifferentialType = EVehicleDifferential4W::LimitedSlip_4W;
		Movement4W->DifferentialSetup.FrontRearSplit = 0.35f;
		Movement4W->DifferentialSetup.FrontLeftRightSplit = 0.5f;
		Movement4W->DifferentialSetup.RearLeftRightSplit = 0.5f;
		Movement4W->DifferentialSetup.CentreBias = 1.3f;
		Movement4W->DifferentialSetup.FrontBias = 1.3f;
		Movement4W->DifferentialSetup.RearBias = 1.3f;

		FRichCurve* SteeringCurve = Movement4W->SteeringCurve.GetRichCurve();
		SteeringCurve->Reset();
		// UE4's curve input is km/h. These are the original UT3 Scorpion steering
		// angles with the speed axis doubled for the new 2300 uu/s UT4 target.
		SteeringCurve->AddKey(0.0f, 1.0f);
		SteeringCurve->AddKey(43.2f, 0.333f);
		SteeringCurve->AddKey(79.2f, 0.222f);
		SteeringCurve->AddKey(93.6f, 0.133f);
		SteeringCurve->AddKey(115.2f, 0.022f);
	}
}

AUTVehicle_Scorpion::AUTVehicle_Scorpion(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bBladesExtended = false;
	bLeftBladeBroken = false;
	bRightBladeBroken = false;
	BladeDamage = 1000.0f;
	BladeTraceRadius = 18.0f;
	LeftBladeStartSocket = FName(TEXT("Blade_L_Start"));
	LeftBladeEndSocket = FName(TEXT("Blade_L_End"));
	RightBladeStartSocket = FName(TEXT("Blade_R_Start"));
	RightBladeEndSocket = FName(TEXT("Blade_R_End"));

	bBoostersActivated = false;
	bSelfDestructArmed = false;
	MaxBoostDuration = 2.0f;
	BoostRechargeDuration = 5.0f;
	BoostTargetSpeed = 3800.0f;
	BoostAcceleration = 7000.0f;
	BoostDamageMultiplier = 1.5f;
	SelfDestructMinBoostTime = 0.15f;
	SelfDestructMinSpeed = 1800.0f;
	SelfDestructFuseDuration = 1.0f;
	SelfDestructDamage = 600.0f;
	SelfDestructRadius = 600.0f;
	SelfDestructMomentum = 200000.0f;
	ArmedDamageMultiplier = 2.0f;
	BoostStartTime = -1000.0f;
	NextBoostTime = -1000.0f;
	bHavePreviousBladePositions = false;
	bHasSelfDestructed = false;
	SelfDestructInstigator = nullptr;

	// Mesh setup
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshFinder(
		TEXT("/Game/RestrictedAssets/Proto/UT3_Vehicles/VH_Scorpion/Meshes/SK_VH_Scorpion_001"));
	if (MeshFinder.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(MeshFinder.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(
		TEXT("/Game/RestrictedAssets/Proto/UT3_Vehicles/VH_Scorpion/Materials/M_Scorpion"));
	if (MatFinder.Succeeded())
	{
		GetMesh()->SetMaterial(0, MatFinder.Object);
	}

	// Vehicle component setup
	if (VehicleComponent != nullptr)
	{
		VehicleComponent->Health = 600;
		VehicleComponent->HealthMax = 600;
		VehicleComponent->bLightArmor = true;
		VehicleComponent->EntryRadius = 350.0f;
	}

	// Full-vehicle chase camera: the old 550 uu arm cropped the rear wheels and
	// read more like a character camera. The longer arm keeps the entire
	// Scorpion visible with some road around it, like a Forza-style chase view.
	if (SpringArm != nullptr)
	{
		SpringArm->TargetArmLength = 1150.0f;
		SpringArm->SetRelativeLocation(FVector(75.0f, 0.0f, 475.0f));
		SpringArm->SetRelativeRotation(FRotator(-7.0f, 0.0f, 0.0f));
		SpringArm->TargetOffset = FVector::ZeroVector;
		SpringArm->bEnableCameraLag = true;
		SpringArm->CameraLagSpeed = 8.0f;
	}
	if (Camera != nullptr)
	{
		Camera->SetFieldOfView(90.0f);
	}

	// Use the original UT3 Scorpion recordings imported into the UT4 project.
	// The engine wave is played directly, so explicitly loop it rather than
	// relying on the AudioComponent to restart a finished one-shot.
	static ConstructorHelpers::FObjectFinder<USoundBase> EngineLoopFinder(
		TEXT("/Game/Mogno/Vehicles/ScorpionTest/SFX/A_Vehicle_Scorpion_EngineLoop01"));
	static ConstructorHelpers::FObjectFinder<USoundBase> EngineStartFinder(
		TEXT("/Game/Mogno/Vehicles/ScorpionTest/SFX/A_Vehicle_Scorpion_Start01"));
	static ConstructorHelpers::FObjectFinder<USoundBase> EngineStopFinder(
		TEXT("/Game/Mogno/Vehicles/ScorpionTest/SFX/A_Vehicle_Scorpion_Stop01"));
	static ConstructorHelpers::FObjectFinder<USoundBase> ImpactFinder(
		TEXT("/Game/Mogno/Vehicles/ScorpionTest/SFX/A_Vehicle_Scorpion_Collide_Cue"));
	EngineLoopSound = EngineLoopFinder.Object;
	EngineStartSound = EngineStartFinder.Object;
	EngineStopSound = EngineStopFinder.Object;
	ImpactSound = ImpactFinder.Object;
	if (USoundWave* EngineLoopWave = Cast<USoundWave>(EngineLoopSound))
	{
		EngineLoopWave->bLooping = true;
	}

	// 4W Movement Component setup
	UWheeledVehicleMovementComponent4W* Movement4W = CastChecked<UWheeledVehicleMovementComponent4W>(GetVehicleMovementComponent());
	ConfigureScorpionDriveFromWorkingBP(Movement4W, GetMesh());

	// Mass override for arcade feel
	Movement4W->Mass = 1200.0f;

	// Lower center of mass so the vehicle doesn't topple.
	// COMNudge shifts the COM relative to the physics body center. Negative Z = lower.
	GetMesh()->BodyInstance.COMNudge = FVector(0.0f, 0.0f, -80.0f);

	// Resist rolling via inertia tensor scaling (higher Y/Z = harder to tip)
	Movement4W->InertiaTensorScale = FVector(1.0f, 1.333f, 1.333f);

	// Note: ThrottleInputRate, BrakeInputRate, SteeringInputRate are protected
	// in UWheeledVehicleMovementComponent. Tune via Blueprint defaults if needed.
}

void AUTVehicle_Scorpion::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	RefreshBladeVisuals();
	if (GetMesh() != nullptr)
	{
		GetMesh()->OnComponentHit.AddUniqueDynamic(this, &AUTVehicle_Scorpion::OnArmedVehicleHit);
	}

	UWheeledVehicleMovementComponent4W* Movement4W = Cast<UWheeledVehicleMovementComponent4W>(GetVehicleMovementComponent());
	if (Movement4W == nullptr)
	{
		return;
	}

	// The existing map actor serialized the earlier native component values,
	// so constructor changes alone do not replace them. Reapply the verified
	// BP setup before rebuilding PhysX on every instance.
	ConfigureScorpionDriveFromWorkingBP(Movement4W, GetMesh());
	if (SpringArm != nullptr)
	{
		SpringArm->TargetArmLength = 1150.0f;
		SpringArm->SetRelativeLocation(FVector(75.0f, 0.0f, 475.0f));
		SpringArm->SetRelativeRotation(FRotator(-7.0f, 0.0f, 0.0f));
		SpringArm->TargetOffset = FVector::ZeroVector;
		SpringArm->bEnableCameraLag = true;
		SpringArm->CameraLagSpeed = 8.0f;
	}
	if (Camera != nullptr)
	{
		Camera->SetFieldOfView(90.0f);
	}

	if (Movement4W->IsRegistered())
	{
		Movement4W->RecreatePhysicsState();
		FBodyInstance* ScorpionChassisBody = GetMesh() != nullptr ? GetMesh()->GetBodyInstance() : nullptr;
		const FString ChassisBoneName = ScorpionChassisBody != nullptr && ScorpionChassisBody->BodySetup != nullptr
			? ScorpionChassisBody->BodySetup->BoneName.ToString()
			: TEXT("NONE");
		const FBox ChassisBounds = ScorpionChassisBody != nullptr && ScorpionChassisBody->BodySetup != nullptr
			? ScorpionChassisBody->BodySetup->AggGeom.CalcAABB(FTransform(FQuat::Identity, FVector::ZeroVector, GetMesh()->GetRelativeTransform().GetScale3D()))
			: FBox(ForceInit);
		UTireConfig* ScorpionTire = UUTWheel_Scorpion_Front::StaticClass()->GetDefaultObject<UUTWheel_Scorpion_Front>()->TireConfig;
		float ScorpionPeakTorque = 0.0f;
		for (const FRichCurveKey& TorqueKey : Movement4W->EngineSetup.TorqueCurve.GetRichCurveConst()->GetCopyOfKeys())
		{
			ScorpionPeakTorque = FMath::Max(ScorpionPeakTorque, TorqueKey.Value);
		}
		UE_LOG(LogTemp, Warning, TEXT("[VehiclePhysics] Applied Scorpion drive setup Physics=%d ChassisBone=%s ChassisZ=(%.2f..%.2f) WheelOffsetZ=(%.2f,%.2f,%.2f,%.2f) MeshClass=%s Tire=%s Friction=%.2f FinalRatio=%.2f PeakTorque=%.1f Diff=%d FrontSplit=%.2f"),
			Movement4W->HasValidPhysicsState() ? 1 : 0,
			*ChassisBoneName,
			ChassisBounds.IsValid ? ChassisBounds.Min.Z : 0.0f,
			ChassisBounds.IsValid ? ChassisBounds.Max.Z : 0.0f,
			Movement4W->WheelSetups[0].AdditionalOffset.Z,
			Movement4W->WheelSetups[1].AdditionalOffset.Z,
			Movement4W->WheelSetups[2].AdditionalOffset.Z,
			Movement4W->WheelSetups[3].AdditionalOffset.Z,
			*GetNameSafe(GetMesh()->GetClass()),
			*GetNameSafe(ScorpionTire),
			ScorpionTire != nullptr ? ScorpionTire->GetFrictionScale() : 1.0f,
			Movement4W->TransmissionSetup.FinalRatio,
			ScorpionPeakTorque,
			(int32)Movement4W->DifferentialSetup.DifferentialType,
			Movement4W->DifferentialSetup.FrontRearSplit);
	}
}

void AUTVehicle_Scorpion::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (Role == ROLE_Authority)
	{
		if (bBladesExtended && !bSelfDestructArmed)
		{
			UpdateBladeTraces();
		}
		else
		{
			bHavePreviousBladePositions = false;
		}

		if (bBoostersActivated || bSelfDestructArmed)
		{
			ApplyBoostForce();
		}
	}
}

void AUTVehicle_Scorpion::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld() != nullptr)
	{
		GetWorldTimerManager().ClearTimer(BoostTimerHandle);
		GetWorldTimerManager().ClearTimer(SelfDestructTimerHandle);
	}
	if (GetMesh() != nullptr)
	{
		GetMesh()->OnComponentHit.RemoveDynamic(this, &AUTVehicle_Scorpion::OnArmedVehicleHit);
	}

	Super::EndPlay(EndPlayReason);
}

void AUTVehicle_Scorpion::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AUTVehicle_Scorpion, bBladesExtended);
	DOREPLIFETIME(AUTVehicle_Scorpion, bLeftBladeBroken);
	DOREPLIFETIME(AUTVehicle_Scorpion, bRightBladeBroken);
	DOREPLIFETIME(AUTVehicle_Scorpion, bBoostersActivated);
	DOREPLIFETIME(AUTVehicle_Scorpion, bSelfDestructArmed);
}

void AUTVehicle_Scorpion::OnHandbrakePressed()
{
	// UT3 puts boost on the jump/handbrake input while accelerating. Preserve
	// the ordinary handbrake when the driver is stopped or reversing.
	if (ThrottleAxisValue > 0.1f)
	{
		RequestBoost();
	}
	else
	{
		Super::OnHandbrakePressed();
	}
}

void AUTVehicle_Scorpion::OnHandbrakeReleased()
{
	Super::OnHandbrakeReleased();
}

void AUTVehicle_Scorpion::OnAltFirePressed()
{
	if (Role == ROLE_Authority)
	{
		SetBladesExtended(true);
	}
	else
	{
		ServerSetBladesExtended(true);
	}
}

void AUTVehicle_Scorpion::OnAltFireReleased()
{
	if (Role == ROLE_Authority)
	{
		SetBladesExtended(false);
	}
	else
	{
		ServerSetBladesExtended(false);
	}
}

bool AUTVehicle_Scorpion::HandleDriverLeaveRequest()
{
	if (Role == ROLE_Authority && ReadyToSelfDestruct())
	{
		return ArmSelfDestructAndEject();
	}

	if (Role == ROLE_Authority)
	{
		StopBoost();
		SetBladesExtended(false);
	}
	return Super::HandleDriverLeaveRequest();
}

bool AUTVehicle_Scorpion::ServerSetBladesExtended_Validate(bool)
{
	return true;
}

void AUTVehicle_Scorpion::ServerSetBladesExtended_Implementation(bool bExtended)
{
	if (VehicleComponent != nullptr && VehicleComponent->HasDriver() && !VehicleComponent->bDead)
	{
		SetBladesExtended(bExtended);
	}
}

void AUTVehicle_Scorpion::SetBladesExtended(bool bExtended)
{
	if (Role != ROLE_Authority)
	{
		return;
	}

	const bool bNewState = bExtended && !bSelfDestructArmed &&
		(!bLeftBladeBroken || !bRightBladeBroken);
	if (bBladesExtended != bNewState)
	{
		bBladesExtended = bNewState;
		bHavePreviousBladePositions = false;
		OnRep_BladeState();
		ForceNetUpdate();
	}
}

void AUTVehicle_Scorpion::OnRep_BladeState()
{
	RefreshBladeVisuals();
	if (!bBladesExtended)
	{
		bHavePreviousBladePositions = false;
	}
}

void AUTVehicle_Scorpion::RefreshBladeVisuals()
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (Mesh == nullptr)
	{
		return;
	}

	const bool bShowLeft = bBladesExtended && !bLeftBladeBroken;
	const bool bShowRight = bBladesExtended && !bRightBladeBroken;
	if (bShowLeft)
	{
		Mesh->UnHideBoneByName(FName(TEXT("Blade_L1")));
		Mesh->UnHideBoneByName(FName(TEXT("Blade_L2")));
	}
	else
	{
		Mesh->HideBoneByName(FName(TEXT("Blade_L1")), PBO_None);
		Mesh->HideBoneByName(FName(TEXT("Blade_L2")), PBO_None);
	}
	if (bShowRight)
	{
		Mesh->UnHideBoneByName(FName(TEXT("Blade_R1")));
		Mesh->UnHideBoneByName(FName(TEXT("Blade_R2")));
	}
	else
	{
		Mesh->HideBoneByName(FName(TEXT("Blade_R1")), PBO_None);
		Mesh->HideBoneByName(FName(TEXT("Blade_R2")), PBO_None);
	}
}

void AUTVehicle_Scorpion::UpdateBladeTraces()
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (Mesh == nullptr || GetWorld() == nullptr)
	{
		return;
	}

	const FVector LeftStart = Mesh->GetSocketLocation(LeftBladeStartSocket);
	const FVector LeftEnd = Mesh->GetSocketLocation(LeftBladeEndSocket);
	const FVector RightStart = Mesh->GetSocketLocation(RightBladeStartSocket);
	const FVector RightEnd = Mesh->GetSocketLocation(RightBladeEndSocket);

	if (bHavePreviousBladePositions)
	{
		if (!bLeftBladeBroken)
		{
			TraceBlade(true, PreviousLeftBladeStart, PreviousLeftBladeEnd, LeftStart, LeftEnd);
		}
		if (!bRightBladeBroken)
		{
			TraceBlade(false, PreviousRightBladeStart, PreviousRightBladeEnd, RightStart, RightEnd);
		}
	}

	PreviousLeftBladeStart = LeftStart;
	PreviousLeftBladeEnd = LeftEnd;
	PreviousRightBladeStart = RightStart;
	PreviousRightBladeEnd = RightEnd;
	bHavePreviousBladePositions = true;
}

void AUTVehicle_Scorpion::TraceBlade(bool bLeftBlade, const FVector& PreviousStart,
	const FVector& PreviousEnd, const FVector& CurrentStart, const FVector& CurrentEnd)
{
	FCollisionQueryParams QueryParams(FName(TEXT("ScorpionBlade")), false, this);
	if (VehicleComponent != nullptr && VehicleComponent->Driver != nullptr)
	{
		QueryParams.AddIgnoredActor(VehicleComponent->Driver);
	}
	const FCollisionShape BladeShape = FCollisionShape::MakeSphere(BladeTraceRadius);

	// The blade moves as a swept segment. Check its current span first, then the
	// socket paths and center path so fast turns cannot tunnel a blade through a
	// character between server frames.
	const FVector PreviousMiddle = (PreviousStart + PreviousEnd) * 0.5f;
	const FVector CurrentMiddle = (CurrentStart + CurrentEnd) * 0.5f;
	const FVector TraceStarts[] = { CurrentStart, PreviousStart, PreviousEnd, PreviousMiddle };
	const FVector TraceEnds[] = { CurrentEnd, CurrentStart, CurrentEnd, CurrentMiddle };
	for (int32 TraceIndex = 0; TraceIndex < 4; ++TraceIndex)
	{
		FHitResult Hit;
		if (GetWorld()->SweepSingleByChannel(Hit, TraceStarts[TraceIndex], TraceEnds[TraceIndex],
			FQuat::Identity, COLLISION_TRACE_WEAPON, BladeShape, QueryParams))
		{
			HandleBladeHit(bLeftBlade, Hit);
			return;
		}
	}
}

void AUTVehicle_Scorpion::HandleBladeHit(bool bLeftBlade, const FHitResult& Hit)
{
	AActor* HitActor = Hit.GetActor();
	if (HitActor == nullptr || HitActor == this)
	{
		BreakBlade(bLeftBlade);
		return;
	}

	// UT3 blades are lethal to infantry but snap on vehicles and scenery.
	// Vehicles are components rather than one concrete pawn hierarchy here.
	if (HitActor->FindComponentByClass<UUTVehicleComponent>() != nullptr)
	{
		BreakBlade(bLeftBlade);
		return;
	}

	APawn* HitPawn = Cast<APawn>(HitActor);
	if (HitPawn == nullptr)
	{
		BreakBlade(bLeftBlade);
		return;
	}

	const FVector BladeDirection = GetVelocity().GetSafeNormal();
	const FVector Momentum = GetVelocity() * 100.0f;
	HitPawn->TakeDamage(BladeDamage,
		FUTPointDamageEvent(BladeDamage, Hit, BladeDirection,
			UUTDmgType_ScorpionBlade::StaticClass(), Momentum),
		GetVehicleDamageInstigator(), this);
}

void AUTVehicle_Scorpion::BreakBlade(bool bLeftBlade)
{
	if (Role != ROLE_Authority)
	{
		return;
	}

	if (bLeftBlade)
	{
		bLeftBladeBroken = true;
	}
	else
	{
		bRightBladeBroken = true;
	}
	if (bLeftBladeBroken && bRightBladeBroken)
	{
		bBladesExtended = false;
	}
	bHavePreviousBladePositions = false;
	OnRep_BladeState();
	ForceNetUpdate();
}

void AUTVehicle_Scorpion::RequestBoost()
{
	if (Role == ROLE_Authority)
	{
		StartBoost();
	}
	else
	{
		ServerStartBoost();
	}
}

bool AUTVehicle_Scorpion::ServerStartBoost_Validate()
{
	return true;
}

void AUTVehicle_Scorpion::ServerStartBoost_Implementation()
{
	StartBoost();
}

bool AUTVehicle_Scorpion::StartBoost()
{
	if (Role != ROLE_Authority || GetWorld() == nullptr || VehicleComponent == nullptr ||
		!VehicleComponent->HasDriver() || VehicleComponent->bDead || bSelfDestructArmed ||
		bBoostersActivated || GetWorld()->GetTimeSeconds() < NextBoostTime)
	{
		return false;
	}

	bBoostersActivated = true;
	BoostStartTime = GetWorld()->GetTimeSeconds();
	GetWorldTimerManager().SetTimer(BoostTimerHandle, this,
		&AUTVehicle_Scorpion::StopBoost, MaxBoostDuration, false);
	OnRep_BoostState();
	ForceNetUpdate();
	return true;
}

void AUTVehicle_Scorpion::StopBoost()
{
	if (Role != ROLE_Authority || bSelfDestructArmed || !bBoostersActivated)
	{
		return;
	}

	bBoostersActivated = false;
	if (GetWorld() != nullptr)
	{
		NextBoostTime = GetWorld()->GetTimeSeconds() + BoostRechargeDuration;
	}
	OnRep_BoostState();
	ForceNetUpdate();
}

void AUTVehicle_Scorpion::ApplyBoostForce()
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (Mesh == nullptr || !Mesh->IsSimulatingPhysics())
	{
		return;
	}

	const FVector Forward = GetActorForwardVector();
	const float ForwardSpeed = FVector::DotProduct(GetVelocity(), Forward);
	if (ForwardSpeed < BoostTargetSpeed)
	{
		const float SpeedAlpha = FMath::Clamp((BoostTargetSpeed - ForwardSpeed) / 1000.0f, 0.15f, 1.0f);
		Mesh->AddForce(Forward * BoostAcceleration * SpeedAlpha, NAME_None, true);
	}
}

bool AUTVehicle_Scorpion::ReadyToSelfDestruct() const
{
	return Role == ROLE_Authority && GetWorld() != nullptr && VehicleComponent != nullptr &&
		VehicleComponent->HasDriver() && !VehicleComponent->bDead && bBoostersActivated &&
		!bSelfDestructArmed && GetWorld()->GetTimeSeconds() - BoostStartTime >= SelfDestructMinBoostTime &&
		GetVelocity().Size() >= SelfDestructMinSpeed;
}

bool AUTVehicle_Scorpion::ArmSelfDestructAndEject()
{
	if (!ReadyToSelfDestruct() || VehicleComponent == nullptr)
	{
		return false;
	}

	SelfDestructInstigator = GetVehicleDamageInstigator();
	bSelfDestructArmed = true;
	bBladesExtended = false;
	VehicleComponent->SetEntryLocked(true);
	GetWorldTimerManager().ClearTimer(BoostTimerHandle);

	// Make the bail decisive enough to clear the car while inheriting its
	// horizontal velocity. The now-empty Scorpion remains boosted during fuse.
	if (!VehicleComponent->EjectDriver(FVector(0.0f, 0.0f, 1200.0f), true))
	{
		bSelfDestructArmed = false;
		VehicleComponent->SetEntryLocked(false);
		SelfDestructInstigator = nullptr;
		GetWorldTimerManager().SetTimer(BoostTimerHandle, this,
			&AUTVehicle_Scorpion::StopBoost,
			FMath::Max(0.01f, MaxBoostDuration - (GetWorld()->GetTimeSeconds() - BoostStartTime)), false);
		return false;
	}

	OnRep_BladeState();
	OnRep_SelfDestructArmed();
	GetWorldTimerManager().SetTimer(SelfDestructTimerHandle, this,
		&AUTVehicle_Scorpion::SelfDestructTimerExpired, SelfDestructFuseDuration, false);
	ForceNetUpdate();
	return true;
}

void AUTVehicle_Scorpion::OnRep_BoostState()
{
	const float NewFOV = (bBoostersActivated || bSelfDestructArmed) ? 105.0f : 90.0f;
	if (Camera != nullptr)
	{
		Camera->SetFieldOfView(NewFOV);
	}
	if (ACameraActor* CameraActor = Cast<ACameraActor>(VehicleCameraActor))
	{
		if (CameraActor->GetCameraComponent() != nullptr)
		{
			CameraActor->GetCameraComponent()->SetFieldOfView(NewFOV);
		}
	}
}

void AUTVehicle_Scorpion::OnRep_SelfDestructArmed()
{
	OnRep_BoostState();
	RefreshBladeVisuals();
}

void AUTVehicle_Scorpion::OnArmedVehicleHit(UPrimitiveComponent*, AActor* OtherActor,
	UPrimitiveComponent*, FVector NormalImpulse, const FHitResult&)
{
	if (Role != ROLE_Authority || !bSelfDestructArmed || bHasSelfDestructed || OtherActor == this)
	{
		return;
	}
	if (SelfDestructInstigator != nullptr && OtherActor == SelfDestructInstigator->GetPawn())
	{
		return;
	}

	const float ImpactSpeed = GetVelocity().Size();
	if (Cast<APawn>(OtherActor) != nullptr || ImpactSpeed >= 900.0f || NormalImpulse.Size() >= 100000.0f)
	{
		SelfDestruct(OtherActor);
	}
}

void AUTVehicle_Scorpion::SelfDestructTimerExpired()
{
	SelfDestruct(nullptr);
}

void AUTVehicle_Scorpion::SelfDestruct(AActor* ImpactedActor)
{
	if (Role != ROLE_Authority || bHasSelfDestructed)
	{
		return;
	}

	bHasSelfDestructed = true;
	GetWorldTimerManager().ClearTimer(BoostTimerHandle);
	GetWorldTimerManager().ClearTimer(SelfDestructTimerHandle);

	AController* DamageInstigator = SelfDestructInstigator != nullptr
		? SelfDestructInstigator
		: GetVehicleDamageInstigator();
	const FVector Origin = GetActorLocation();
	TArray<AActor*> IgnoreActors;
	APawn* InstigatorPawn = DamageInstigator != nullptr ? DamageInstigator->GetPawn() : nullptr;
	if (InstigatorPawn != nullptr)
	{
		// UT3's self-destruct damage type explicitly does not hurt the driver who
		// armed it; the launched character can still be inside the 600 uu radius.
		IgnoreActors.Add(InstigatorPawn);
	}
	if (ImpactedActor != nullptr)
	{
		if (ImpactedActor != InstigatorPawn)
		{
			const FVector ImpactDirection = (ImpactedActor->GetActorLocation() - Origin).GetSafeNormal();
			const FHitResult ImpactHit(ImpactedActor, nullptr, ImpactedActor->GetActorLocation(), -ImpactDirection);
			ImpactedActor->TakeDamage(SelfDestructDamage,
				FUTPointDamageEvent(SelfDestructDamage, ImpactHit, ImpactDirection,
					UUTDmgType_ScorpionSelfDestruct::StaticClass(), ImpactDirection * SelfDestructMomentum),
				DamageInstigator, this);
			IgnoreActors.AddUnique(ImpactedActor);
		}
	}

	UUTGameplayStatics::UTHurtRadius(this, SelfDestructDamage, 0.0f, SelfDestructMomentum,
		Origin, 0.0f, SelfDestructRadius, 1.0f,
		UUTDmgType_ScorpionSelfDestruct::StaticClass(), IgnoreActors, this, DamageInstigator);

	bBoostersActivated = false;
	bSelfDestructArmed = false;
	if (VehicleComponent != nullptr)
	{
		VehicleComponent->Health = 0;
		if (!VehicleComponent->bDead)
		{
			VehicleComponent->VehicleDied(DamageInstigator);
		}
	}
	SetActorEnableCollision(false);
	if (GetMesh() != nullptr)
	{
		GetMesh()->SetSimulatePhysics(false);
		GetMesh()->SetVisibility(false, true);
	}
	SetLifeSpan(0.25f);
	ForceNetUpdate();
}

AController* AUTVehicle_Scorpion::GetVehicleDamageInstigator() const
{
	if (Controller != nullptr)
	{
		return Controller;
	}
	return VehicleComponent != nullptr ? VehicleComponent->DamageInstigator : nullptr;
}

float AUTVehicle_Scorpion::TakeDamage(float Damage, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	const bool bWasArmed = bSelfDestructArmed;
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

	// Apply the boost vulnerability only after radial falloff, then let the
	// shared component apply the weapon's UT3 vehicle-damage multiplier.
	ResolvedDamage *= bSelfDestructArmed ? ArmedDamageMultiplier
		: (bBoostersActivated ? BoostDamageMultiplier : 1.0f);
	const float AppliedDamage = VehicleComponent != nullptr
		? VehicleComponent->ApplyDamage(ResolvedDamage, DamageEvent, EventInstigator, DamageCauser)
		: 0.0f;
	if (Role == ROLE_Authority && bWasArmed && AppliedDamage > 0.0f &&
		VehicleComponent != nullptr && VehicleComponent->bDead && !bHasSelfDestructed)
	{
		SelfDestruct(nullptr);
	}
	else if (Role == ROLE_Authority && VehicleComponent != nullptr && VehicleComponent->bDead)
	{
		StopBoost();
		SetBladesExtended(false);
	}
	return AppliedDamage;
}

// ---- Wheel classes ----

UUTWheel_Scorpion_Front::UUTWheel_Scorpion_Front()
{
	static ConstructorHelpers::FObjectFinder<UTireConfig> TireFinder(
		TEXT("/Game/Mogno/Vehicles/ScorpionTest/DataConfig_ScorpionTyre"));
	if (TireFinder.Succeeded())
	{
		TireConfig = TireFinder.Object;
	}

	ShapeRadius = 45.0f;
	ShapeWidth = 30.0f;
	Mass = 20.0f;
	DampingRate = 0.25f;
	SteerAngle = 40.0f;
	bAffectedByHandbrake = false;

	// Tire grip
	LatStiffMaxLoad = 2.0f;
	LatStiffValue = 25.0f;
	LongStiffValue = 1500.0f;

	// Suspension - generous travel for UT3 mesh scale
	SuspensionForceOffset = 0.0f;
	SuspensionMaxRaise = 40.0f;
	SuspensionMaxDrop = 40.0f;
	SuspensionNaturalFrequency = 9.0f;
	SuspensionDampingRatio = 1.5f;

	// Brakes
	MaxBrakeTorque = 3000.0f;
	MaxHandBrakeTorque = 0.0f;
}

UUTWheel_Scorpion_Rear::UUTWheel_Scorpion_Rear()
{
	static ConstructorHelpers::FObjectFinder<UTireConfig> TireFinder(
		TEXT("/Game/Mogno/Vehicles/ScorpionTest/DataConfig_ScorpionTyre"));
	if (TireFinder.Succeeded())
	{
		TireConfig = TireFinder.Object;
	}

	ShapeRadius = 45.0f;
	ShapeWidth = 30.0f;
	Mass = 20.0f;
	DampingRate = 0.25f;
	SteerAngle = 0.0f;
	bAffectedByHandbrake = true;

	// Tire grip
	LatStiffMaxLoad = 2.0f;
	LatStiffValue = 25.0f;
	LongStiffValue = 1500.0f;

	// Suspension - generous travel for UT3 mesh scale
	SuspensionForceOffset = 0.0f;
	SuspensionMaxRaise = 40.0f;
	SuspensionMaxDrop = 40.0f;
	SuspensionNaturalFrequency = 9.0f;
	SuspensionDampingRatio = 1.5f;

	// Brakes
	MaxBrakeTorque = 2000.0f;
	MaxHandBrakeTorque = 6000.0f;
}
