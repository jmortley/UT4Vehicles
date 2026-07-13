#pragma once

#include "WheeledVehicle.h"
#include "UTTeamInterface.h"
#include "UTVehicleComponent.h"
#include "UTVehicle.generated.h"

class AUTHUD;
class UCanvas;

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

	// IUTTeamInterface
	virtual uint8 GetTeamNum() const override;
	virtual void SetTeamForSideSwap_Implementation(uint8 NewTeamNum) override;

	// AActor
	virtual void PostInitializeComponents() override;

	// APawn
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void PawnClientRestart() override;

	// AActor
	virtual void PostRender(class AUTHUD* HUD, UCanvas* Canvas);

	/** Input handlers */
	void OnThrottleInput(float Value);
	void OnSteeringInput(float Value);
	void OnBrakeInput(float Value);
	void OnHandbrakePressed();
	void OnHandbrakeReleased();

	/** Server RPC: request to enter this vehicle */
	UFUNCTION(Reliable, Server, WithValidation)
	void ServerTryEnter(APawn* NewDriver);

	/** Server RPC: request to exit this vehicle */
	UFUNCTION(Reliable, Server, WithValidation)
	void ServerDriverLeave();

	// ---- Option B prep ----
	// To switch from auto-enter to key-press entry:
	// 1. Add +ActionMappings=(ActionName="EnterVehicle",Key=E,...) to DefaultInput.ini
	// 2. Remove the TryToDrive call from UTVehicleComponent::OnEntryTriggerBeginOverlap
	// 3. Uncomment Tick below — it checks overlapping pawns each frame for E key press
	//    and calls ServerTryEnter when detected.
	//
	// virtual void Tick(float DeltaTime) override;
};
