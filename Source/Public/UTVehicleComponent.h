#pragma once

#include "Components/SphereComponent.h"
#include "UTVehicleComponent.generated.h"

class AUTCharacter;
class AUTHUD;
class APlayerController;
class AController;
class APawn;
class UCanvas;
class UInputComponent;
class USoundBase;
class USoundAttenuation;

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

	/** Prevents new occupants while an ability owns the empty vehicle. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_EntryLocked, Category = Vehicle)
	bool bEntryLocked;

	/** Radius within which players can enter */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Vehicle)
	float EntryRadius;

	/** Respawn delay after destruction */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Vehicle)
	float RespawnDelay;

	/** Entry trigger sphere */
	UPROPERTY()
	USphereComponent* EntryTrigger;

	/** Vehicle horn one-shot. Current Axon vehicles share UT3's small-human horn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vehicle|Audio")
	USoundBase* HornSound;

	/** Try to let a pawn drive this vehicle. Authority only. */
	bool TryToDrive(APawn* NewDriver);

	/** Shared vacancy/range/team/ability gate used by prompts and authority. */
	bool CanEnterVehicle(APawn* NewDriver) const;

	/** Authority-only entry lock, used by armed or otherwise unavailable vehicles. */
	void SetEntryLocked(bool bLocked);

	/** Execute driver entering the vehicle */
	bool DriverEnter(APawn* NewDriver);

	/** Execute driver leaving the vehicle */
	bool DriverLeave(bool bForceLeave);

	/**
	 * Force the driver out above the chassis, inherit planar vehicle velocity,
	 * add EjectVelocity, and leave the character in falling movement.
	 */
	bool EjectDriver(const FVector& EjectVelocity, bool bInheritVehicleVelocity = true);

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

	/** Request a networked horn blast from the owning driver. */
	void RequestHorn();

	UFUNCTION(Reliable, Server, WithValidation)
	void ServerRequestHorn();

	UFUNCTION(Reliable, NetMulticast)
	void MulticastPlayHorn();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	UFUNCTION()
	void OnRep_Driver();

	UFUNCTION()
	void OnRep_EntryLocked();

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
	void RefreshEntryInput();
	void OnActivateSpecialPressed();
	void EnsureEntryProxy(AController* Controller);
	bool DriverLeaveInternal(bool bForceLeave, bool bEject, const FVector& EjectVelocity, bool bInheritVehicleVelocity);
	bool FindSafeExitLocation(AUTCharacter* Character, FVector& OutLocation, FRotator& OutRotation) const;

	/** Saved MaxSafeFallSpeed from driver character, restored on exit */
	float SavedMaxSafeFallSpeed;

	/** Runtime 3D attenuation shared by horn playback on every client. */
	UPROPERTY(Transient)
	USoundAttenuation* HornAttenuation;

	float LastHornTime;
	void PlayHornAuthoritative();
};
