#pragma once

#include "UTVehicle.h"
#include "VehicleWheel.h"
#include "UTVehicle_Scorpion.generated.h"

class UParticleSystem;
class UStaticMesh;
class USoundBase;
class AUTImpactEffect;
class AUTProjectile;
class AUTProj_ScorpionGlob;

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Gun")
	TSubclassOf<AUTProjectile> GunProjectileClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Gun")
	FName GunMuzzleSocket;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Gun")
	float GunFireInterval;

	/** Fast visual tracking with interpolation between replicated aim packets. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Gun")
	float GunAimRotationRate;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Gun")
	UParticleSystem* GunMuzzleEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Gun")
	USoundBase* GunFireSound;

	/** UT3 Scorpion takes 50% more damage while its ordinary boost is active. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Boost")
	float BoostDamageMultiplier;

	/** Maximum extra downward acceleration while the chassis is close to drivable ground. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Handling")
	float GroundDownforceAcceleration;

	/** Forward speed at which the full ground-downforce acceleration is applied. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Handling")
	float GroundDownforceTargetSpeed;

	/** Ground distance used to disengage downforce cleanly when the Scorpion jumps. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Handling")
	float GroundStabilityTraceDistance;

	/** Angular acceleration that resists chassis roll and pitch relative to the road. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Handling")
	float GroundUprightAcceleration;

	/** Direct roll damping used only while the chassis is close to drivable ground. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Handling")
	float GroundRollDamping;

	/** Direct pitch damping used only while the chassis is close to drivable ground. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Handling")
	float GroundPitchDamping;

	/** Direct planar deceleration applied after an ordinary driver exit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Handling")
	float VacantStopDeceleration;

	/** UT4-scaled normal driving target; boost has its own higher target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Handling")
	float NormalDriveTargetSpeed;

	/** Arcade acceleration assist that carries the heavy chassis to its target speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Handling")
	float NormalDriveAcceleration;

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

	/** Original UT3 boost-eject explosion, imported into the UT4 content tree. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Self Destruct")
	UParticleSystem* SelfDestructEffect;

	/** Full UT4 explosion layered under the imported UT3 fire/smoke system. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Self Destruct")
	TSubclassOf<AUTImpactEffect> SelfDestructBlastEffect;

	/** World scale for the imported UT3 fire/smoke system. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Self Destruct")
	float SelfDestructEffectScale;

	/** World scale for the full flash, fireball, smoke, and audio blast. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Self Destruct")
	float SelfDestructBlastScale;

	/** Locally simulated hatch debris spawned for the boost-eject explosion. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Self Destruct")
	UStaticMesh* SelfDestructCanopyMesh;

	/** Imported UT3 skeleton bone at the center of the detachable hatch. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Self Destruct")
	FName SelfDestructCanopyBone;

	/** Hatch launch velocity in the Scorpion mesh's local coordinate space. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Self Destruct")
	FVector SelfDestructCanopyLaunchVelocity;

	/** Hatch angular velocity in the Scorpion mesh's local coordinate space. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Self Destruct")
	FVector SelfDestructCanopyAngularVelocity;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Self Destruct")
	float SelfDestructCanopyLifetime;

	/** An armed, driverless Scorpion takes double damage so it can be intercepted. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scorpion|Self Destruct")
	float ArmedDamageMultiplier;

	/** Client-safe HUD predicate for when ActivateSpecial will arm boost-eject. */
	bool IsBoostEjectReady() const;

	/** Replication-safe HUD predicate shown for the full ordinary boost window. */
	bool ShouldShowBoostEjectPrompt() const;

protected:
	virtual void OnThrottleInput(float Value) override;
	virtual void OnHandbrakePressed() override;
	virtual void OnHandbrakeReleased() override;
	virtual void OnPrimaryFirePressed() override;
	virtual void OnPrimaryFireReleased() override;
	virtual void OnAltFirePressed() override;
	virtual void OnAltFireReleased() override;
	virtual bool HandleDriverLeaveRequest() override;
	virtual bool ShouldApplyVacantBrake() const override;

	UFUNCTION(Reliable, Server, WithValidation)
	void ServerSetBladesExtended(bool bExtended);

	UFUNCTION(Reliable, Server, WithValidation)
	void ServerSetGunFiring(bool bNewFiring, FRotator NewAimRotation);

	UFUNCTION(Unreliable, Server, WithValidation)
	void ServerSetGunAim(FRotator NewAimRotation);

	UFUNCTION(Reliable, Server, WithValidation)
	void ServerStartBoost();

	/** Replicate the owning client's forward input for authoritative speed assist. */
	UFUNCTION(Reliable, Server, WithValidation)
	void ServerSetNormalDriveThrottle(float NewThrottle);

	UFUNCTION()
	void OnRep_BladeState();

	UFUNCTION()
	void OnRep_BoostState();

	UFUNCTION()
	void OnRep_SelfDestructArmed();

	UFUNCTION()
	void OnRep_GunAim();

	UFUNCTION(Unreliable, NetMulticast)
	void MulticastPlayGunFire(FVector MuzzleLocation, FRotator MuzzleRotation);

	/** Play the one-shot destruction presentation on every relevant client. */
	UFUNCTION(Reliable, NetMulticast)
	void MulticastPlaySelfDestructEffects(FVector EffectLocation, FRotator EffectRotation,
		FVector CanopyLocation, FRotator CanopyRotation, FVector InheritedVelocity);

	UFUNCTION()
	void OnArmedVehicleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	void SetBladesExtended(bool bExtended);
	void SetGunFiring(bool bNewFiring);
	void FireGun();
	void UpdateGunAim(float DeltaSeconds);
	FRotator GetDesiredGunAimRotation() const;
	FRotator GetGunAimRotation() const;
	FRotator GetAdjustedProjectileAimRotation() const;
	FRotator SanitizeGunAim(const FRotator& AimRotation) const;
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
	void ApplyNormalDriveAssist();
	void ApplyGroundStability(float DeltaSeconds);
	void ApplyVacantStop(float DeltaSeconds);
	bool ReadyToSelfDestruct() const;
	bool ArmSelfDestructAndEject();
	void SelfDestructTimerExpired();
	void SelfDestruct(AActor* ImpactedActor);
	AController* GetVehicleDamageInstigator() const;

	float BoostStartTime;
	float NextBoostTime;
	float NextGunFireTime;
	float LastGunAimSendTime;
	float NormalDriveThrottle;
	float LastSentNormalDriveThrottle;
	bool bHavePreviousBladePositions;
	bool bHasSelfDestructed;
	bool bGunFiring;
	FRotator CurrentGunAim;
	FRotator LastSentGunAim;

	UPROPERTY(ReplicatedUsing = OnRep_GunAim)
	FRotator ReplicatedGunAim;
	FVector PreviousLeftBladeStart;
	FVector PreviousLeftBladeEnd;
	FVector PreviousRightBladeStart;
	FVector PreviousRightBladeEnd;

	UPROPERTY(Transient)
	AController* SelfDestructInstigator;

	FTimerHandle BoostTimerHandle;
	FTimerHandle SelfDestructTimerHandle;
	FTimerHandle GunFireTimerHandle;
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
