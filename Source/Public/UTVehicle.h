#pragma once

#include "WheeledVehicle.h"
#include "UTTeamInterface.h"
#include "UTVehicleComponent.h"
#include "UTVehicle.generated.h"

class AUTHUD;
class UCanvas;
class UInputComponent;
class UAudioComponent;
class USoundBase;
class USoundAttenuation;
class UPrimitiveComponent;

/**
 * Base class for wheeled vehicles in UT4.
 * Uses the PhysX vehicle system with built-in replication.
 * Shared vehicle logic (enter/exit, health, damage, team) is in UTVehicleComponent.
 */
UCLASS(Abstract)
class UTVEHICLES_API AUTVehicle : public AWheeledVehicle, public IUTTeamInterface
{
	GENERATED_UCLASS_BODY()

public:
	/** Shared vehicle component handling enter/exit, health, damage */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Vehicle)
	UUTVehicleComponent* VehicleComponent;

	/** Spring arm for third-person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera)
	class USpringArmComponent* SpringArm;

	/** Camera component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera)
	class UCameraComponent* Camera;

	/** Spatial engine loop whose pitch follows PhysX engine RPM. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Audio)
	UAudioComponent* EngineAudioComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Audio)
	USoundBase* EngineLoopSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Audio)
	USoundBase* EngineStartSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Audio)
	USoundBase* EngineStopSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Audio)
	USoundBase* ImpactSound;

	/** Shared 3D falloff used by the loop and one-shot vehicle sounds. */
	UPROPERTY(Transient)
	USoundAttenuation* VehicleSoundAttenuation;

	// IUTTeamInterface
	virtual uint8 GetTeamNum() const override;
	virtual void SetTeamForSideSwap_Implementation(uint8 NewTeamNum) override;

	// AActor
	virtual void PostInitializeComponents() override;
	virtual void Tick(float DeltaSeconds) override;

	// APawn
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void PawnClientRestart() override;

	// AActor
	virtual void PostRender(class AUTHUD* HUD, UCanvas* Canvas);
	virtual void PostRenderFor(APlayerController* PC, UCanvas* Canvas, FVector CameraPosition, FVector CameraDir) override;

	// APawn
	virtual void UnPossessed() override;
	virtual void OnRep_Controller() override;

	// AActor
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Input handlers */
	void OnThrottleInput(float Value);
	void OnReverseInput(float Value);
	void OnSteeringInput(float Value);
	void OnSteerLeftInput(float Value);
	void OnHandbrakePressed();
	void OnHandbrakeReleased();
	void PlayEnterSound();
	void PlayExitSound();

	/** Server RPC: request to enter this vehicle */
	UFUNCTION(Reliable, Server, WithValidation)
	void ServerTryEnter(APawn* NewDriver);

	/** Server RPC: request to exit this vehicle */
	UFUNCTION(Reliable, Server, WithValidation)
	void ServerDriverLeave();

protected:
	/**
	 * Local camera view target. UT's camera manager forces a possessed pawn that
	 * is not an AUTCharacter into FirstPerson, but explicitly honors an
	 * ACameraActor as a normal component-driven view. This actor is attached to
	 * Camera, so it inherits the SpringArm orbit and collision result exactly.
	 */
	UPROPERTY(Transient)
	AActor* VehicleCameraActor;

	// AUTPlayerController binds the movement axes on its own InputComponent
	// (higher in the input stack than the pawn's) and consumes the keys while
	// its handlers route to a null UTCharacter during driving — so pawn-level
	// bindings only ever receive zeros. Driving uses a dedicated component
	// explicitly pushed above the possessing controller's component.
	void BindDrivingInput();
	void UnbindDrivingInput();
	void ActivateVehicleCamera();
	void DeactivateVehicleCamera();
	void UpdateVehicleAudio(float DeltaSeconds);

	UFUNCTION()
	void OnVehicleMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	/** Combine the four cached axis values into throttle/brake/steering */
	void ApplyDriveInput();

	/** Controller whose input stack currently carries our capture component */
	UPROPERTY()
	class APlayerController* BoundInputPC;

	/**
	 * Dedicated high-priority input component, mirroring the working BP
	 * VehicleInputCaptureHellbender actor. Keeping the bindings off both the
	 * pawn and controller components prevents UT's controller bindings from
	 * consuming the movement keys first.
	 */
	UPROPERTY(Transient)
	UInputComponent* DrivingInputComponent;

	float ThrottleAxisValue;
	float ReverseAxisValue;
	float SteerRightAxisValue;
	float SteerLeftAxisValue;
	float NextDriveInputLogTime;
	float LastImpactSoundTime;
};
