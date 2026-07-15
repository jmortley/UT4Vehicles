#pragma once

#include "UTVehicle.h"
#include "VehicleWheel.h"
#include "UTVehicle_Scorpion.generated.h"

/**
 * UT3 Scorpion - 4-wheeled ground vehicle with rear-wheel drive.
 * Uses SK_VH_Scorpion_001 skeletal mesh from UT3 assets.
 */
UCLASS()
class UTVEHICLES_API AUTVehicle_Scorpion : public AUTVehicle
{
	GENERATED_UCLASS_BODY()

public:
	virtual void PostInitializeComponents() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Blade state is authoritative; visuals and traces use the mesh's imported UT3 sockets. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_BladeState, Category = "Scorpion|Blades")
	bool bBladesExtended;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_BladeState, Category = "Scorpion|Blades")
	bool bLeftBladeBroken;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_BladeState, Category = "Scorpion|Blades")
	bool bRightBladeBroken;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Blades")
	float BladeDamage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Blades")
	float BladeTraceRadius;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Blades")
	FName LeftBladeStartSocket;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Blades")
	FName LeftBladeEndSocket;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Blades")
	FName RightBladeStartSocket;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Blades")
	FName RightBladeEndSocket;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_BoostState, Category = "Scorpion|Boost")
	bool bBoostersActivated;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_SelfDestructArmed, Category = "Scorpion|Boost")
	bool bSelfDestructArmed;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Boost")
	float MaxBoostDuration;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Boost")
	float BoostRechargeDuration;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Boost")
	float BoostTargetSpeed;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Boost")
	float BoostAcceleration;

	/** UT3 Scorpion takes 50% more damage while its ordinary boost is active. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Boost")
	float BoostDamageMultiplier;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Self Destruct")
	float SelfDestructMinBoostTime;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Self Destruct")
	float SelfDestructMinSpeed;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Self Destruct")
	float SelfDestructFuseDuration;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Self Destruct")
	float SelfDestructDamage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Self Destruct")
	float SelfDestructRadius;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Self Destruct")
	float SelfDestructMomentum;

	/** An armed, driverless Scorpion takes double damage so it can be intercepted. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Self Destruct")
	float ArmedDamageMultiplier;

protected:
	virtual void OnHandbrakePressed() override;
	virtual void OnHandbrakeReleased() override;
	virtual void OnAltFirePressed() override;
	virtual void OnAltFireReleased() override;
	virtual bool HandleDriverLeaveRequest() override;

	UFUNCTION(Reliable, Server, WithValidation)
	void ServerSetBladesExtended(bool bExtended);

	UFUNCTION(Reliable, Server, WithValidation)
	void ServerStartBoost();

	UFUNCTION()
	void OnRep_BladeState();

	UFUNCTION()
	void OnRep_BoostState();

	UFUNCTION()
	void OnRep_SelfDestructArmed();

	UFUNCTION()
	void OnArmedVehicleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	void SetBladesExtended(bool bExtended);
	void RefreshBladeVisuals();
	void UpdateBladeTraces();
	void TraceBlade(bool bLeftBlade, const FVector& PreviousStart, const FVector& PreviousEnd,
		const FVector& CurrentStart, const FVector& CurrentEnd);
	void HandleBladeHit(bool bLeftBlade, const FHitResult& Hit);
	void BreakBlade(bool bLeftBlade);

	void RequestBoost();
	bool StartBoost();
	void StopBoost();
	void ApplyBoostForce();
	bool ReadyToSelfDestruct() const;
	bool ArmSelfDestructAndEject();
	void SelfDestructTimerExpired();
	void SelfDestruct(AActor* ImpactedActor);
	AController* GetVehicleDamageInstigator() const;

	float BoostStartTime;
	float NextBoostTime;
	bool bHavePreviousBladePositions;
	bool bHasSelfDestructed;
	FVector PreviousLeftBladeStart;
	FVector PreviousLeftBladeEnd;
	FVector PreviousRightBladeStart;
	FVector PreviousRightBladeEnd;

	UPROPERTY(Transient)
	AController* SelfDestructInstigator;

	FTimerHandle BoostTimerHandle;
	FTimerHandle SelfDestructTimerHandle;
};

/** Front wheel for Scorpion - steerable, no handbrake */
UCLASS()
class UTVEHICLES_API UUTWheel_Scorpion_Front : public UVehicleWheel
{
	GENERATED_BODY()

public:
	UUTWheel_Scorpion_Front();
};

/** Rear wheel for Scorpion - driven, handbrake affected */
UCLASS()
class UTVEHICLES_API UUTWheel_Scorpion_Rear : public UVehicleWheel
{
	GENERATED_BODY()

public:
	UUTWheel_Scorpion_Rear();
};
