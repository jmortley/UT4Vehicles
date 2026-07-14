#pragma once

#include "UTTeamInterface.h"
#include "UTVehicleComponent.h"
#include "UTVehicleMovementHover.h"
#include "UTVehicleFlying.generated.h"

class AUTHUD;
class UCanvas;

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
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void PawnClientRestart() override;
	virtual UPawnMovementComponent* GetMovementComponent() const override;

	// AActor
	virtual void PostRender(class AUTHUD* HUD, UCanvas* Canvas);
	virtual void PostRenderFor(APlayerController* PC, UCanvas* Canvas, FVector CameraPosition, FVector CameraDir) override;

	/** Input handlers */
	void OnThrottleInput(float Value);
	void OnSteeringInput(float Value);
	void OnLiftUp();
	void OnLiftUpRelease();
	void OnLiftDown();
	void OnLiftDownRelease();
	void OnMousePitch(float Value);

	/** Server RPC: request to enter this vehicle */
	UFUNCTION(Reliable, Server, WithValidation)
	void ServerTryEnter(APawn* NewDriver);

	/** Server RPC: request to exit this vehicle */
	UFUNCTION(Reliable, Server, WithValidation)
	void ServerDriverLeave();

protected:
	/** Tracking lift input state */
	bool bLiftUp;
	bool bLiftDown;
};
