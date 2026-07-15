#pragma once

#include "UTTeamInterface.h"
#include "UTVehicleComponent.h"
#include "UTVehicleMovementHover.h"
#include "UTVehicleFlying.generated.h"

class AUTHUD;
class UCanvas;
class UInputComponent;
class APlayerController;

/**
 * Base class for flying/hover vehicles in UT4 (Raptor, Manta, Fury, Cicada).
 * Inherits from APawn directly (not AWheeledVehicle) and uses UUTVehicleMovementHover.
 * Shared vehicle logic is in UTVehicleComponent.
 */
UCLASS(Abstract)
class UTVEHICLES_API AUTVehicleFlying : public APawn, public IUTTeamInterface
{
	GENERATED_UCLASS_BODY()

public:
	/** Vehicle mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Vehicle)
	USkeletalMeshComponent* VehicleMesh;

	/** Hover/flight movement component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Movement)
	UUTVehicleMovementHover* HoverMovement;

	/** Shared vehicle component handling enter/exit, health, damage */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Vehicle)
	UUTVehicleComponent* VehicleComponent;

	/** Spring arm for third-person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera)
	class USpringArmComponent* SpringArm;

	/** Camera component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera)
	class UCameraComponent* Camera;

	// IUTTeamInterface
	virtual uint8 GetTeamNum() const override;
	virtual void SetTeamForSideSwap_Implementation(uint8 NewTeamNum) override;

	// APawn
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void PawnClientRestart() override;
	virtual void PawnStartFire(uint8 FireModeNum = 0) override;
	/** Explicit stop dispatcher for AI and other non-player vehicle controllers. */
	void StopVehicleFire(uint8 FireModeNum = 0);
	virtual UPawnMovementComponent* GetMovementComponent() const override;
	virtual void UnPossessed() override;
	virtual void OnRep_Controller() override;

	// AActor
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostRender(class AUTHUD* HUD, UCanvas* Canvas);
	virtual void PostRenderFor(APlayerController* PC, UCanvas* Canvas, FVector CameraPosition, FVector CameraDir) override;

	/** Input handlers */
	void OnThrottleInput(float Value);
	void OnReverseInput(float Value);
	void OnSteeringInput(float Value);
	void OnSteerLeftInput(float Value);
	void OnLiftUp();
	void OnLiftUpRelease();
	void OnLiftDown();
	void OnLiftDownRelease();
	void OnMousePitch(float Value);
	virtual void OnPrimaryFirePressed();
	virtual void OnPrimaryFireReleased();
	virtual void OnAltFirePressed();
	virtual void OnAltFireReleased();
	void OnHornPressed();

	/** Server RPC: request to exit this vehicle */
	UFUNCTION(Reliable, Server, WithValidation)
	void ServerDriverLeave();

protected:
	void BindDrivingInput();
	void UnbindDrivingInput();
	virtual void BindVehicleSpecificInput(UInputComponent* InputComponent);
	virtual bool HandleDriverLeaveRequest();
	void HandlePrimaryFirePressed();
	void HandlePrimaryFireReleased();
	void HandleAltFirePressed();
	void HandleAltFireReleased();
	void ApplyFlightInput();
	void ActivateVehicleCamera();
	void DeactivateVehicleCamera();

	/** CameraActor view target used to bypass UT's non-character pawn camera. */
	UPROPERTY(Transient)
	AActor* VehicleCameraActor;

	/** Controller whose input stack currently carries our capture component. */
	UPROPERTY(Transient)
	APlayerController* BoundInputPC;

	/** Input component pushed above AUTPlayerController's consuming bindings. */
	UPROPERTY(Transient)
	UInputComponent* DrivingInputComponent;

	/** Tracking lift input state */
	bool bLiftUp;
	bool bLiftDown;
	float ThrottleAxisValue;
	float ReverseAxisValue;
	float SteerRightAxisValue;
	float SteerLeftAxisValue;
	bool bPrimaryFireKeyDown;
	bool bAltFireKeyDown;
	bool bPrimaryFireInputDown;
	bool bAltFireInputDown;
};
