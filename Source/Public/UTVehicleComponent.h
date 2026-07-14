#pragma once

#include "Components/SphereComponent.h"
#include "UTVehicleComponent.generated.h"

class AUTCharacter;
class AUTHUD;
class APlayerController;
class APawn;
class UCanvas;
class UInputComponent;

UCLASS(meta = (BlueprintSpawnableComponent))
class UTVEHICLES_API UUTVehicleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UUTVehicleComponent();

	/** The character currently driving this vehicle */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Driver, Category = Vehicle)
	APawn* Driver;

	/** Controller that gets credit for damage (stored before possession transfer) */
	UPROPERTY()
	AController* DamageInstigator;

	/** Current health */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = Vehicle)
	int32 Health;

	/** Maximum health */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Replicated, Category = Vehicle)
	int32 HealthMax;

	/** Team number (255 = no team) */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = Vehicle)
	uint8 TeamNum;

	/** Is the vehicle destroyed */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = Vehicle)
	bool bDead;

	/** Radius within which players can enter */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Vehicle)
	float EntryRadius;

	/** Respawn delay after destruction */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Vehicle)
	float RespawnDelay;

	/** Entry trigger sphere */
	UPROPERTY()
	USphereComponent* EntryTrigger;

	/** Try to let a pawn drive this vehicle. Authority only. */
	bool TryToDrive(APawn* NewDriver);

	/** Execute driver entering the vehicle */
	bool DriverEnter(APawn* NewDriver);

	/** Execute driver leaving the vehicle */
	bool DriverLeave(bool bForceLeave);

	/** Apply damage to the vehicle */
	float ApplyDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser);

	/** Called when vehicle is destroyed */
	void VehicleDied(AController* Killer);

	/** Draw vehicle HUD (health bar, speed, enter prompt) */
	void DrawVehicleHUD(AUTHUD* HUD, UCanvas* Canvas, APlayerController* ViewingPC);
	void DrawEntryPrompt(AUTHUD* HUD, UCanvas* Canvas, APlayerController* ViewingPC);

	/** Set up the entry trigger sphere on the owning actor */
	void InitEntryTrigger();

	UFUNCTION()
	void OnEntryTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnEntryTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/** Is the given pawn overlapping the entry trigger? */
	bool IsInEntryRange(APawn* TestPawn) const;

	bool HasDriver() const { return Driver != nullptr; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	UFUNCTION()
	void OnRep_Driver();

	/** Pawns currently overlapping the entry trigger */
	UPROPERTY()
	TArray<APawn*> OverlappingPawns;

	/** High-priority local binding that consumes ActivateSpecial while in range. */
	UPROPERTY(Transient)
	UInputComponent* EntryInputComponent;

	UPROPERTY(Transient)
	APlayerController* EntryInputPC;

	UPROPERTY(Transient)
	APawn* EntryInputPawn;

	/** Driver whose hidden/collision state was changed locally. */
	UPROPERTY(Transient)
	APawn* VisualDriver;

	void SetDriverVisualState(APawn* DriverPawn, bool bDriving);
	void BindEntryInput(APawn* LocalPawn);
	void UnbindEntryInput();
	void OnActivateSpecialPressed();
	void RefreshEntryOwner();

	/** Saved MaxSafeFallSpeed from driver character, restored on exit */
	float SavedMaxSafeFallSpeed;
};
