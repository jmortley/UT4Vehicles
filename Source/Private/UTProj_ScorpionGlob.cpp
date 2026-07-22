#include "UTProj_ScorpionGlob.h"
#include "UnrealTournament.h"
#include "UTVehicleDamageType.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

AUTProj_ScorpionGlob::AUTProj_ScorpionGlob(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (CollisionComp != nullptr)
	{
		CollisionComp->InitSphereRadius(18.0f);
	}
	OverlapRadius = 36.0f;
	if (PawnOverlapSphere != nullptr)
	{
		PawnOverlapSphere->InitSphereRadius(OverlapRadius);
	}

	FlightEffectComponent = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("FlightEffect"));
	FlightEffectComponent->SetupAttachment(RootComponent);
	FlightEffectComponent->bAutoActivate = true;
	static ConstructorHelpers::FObjectFinder<UParticleSystem> FlightEffectFinder(
		TEXT("/Game/RestrictedAssets/Weapons/LinkGun/Effects/P_WP_Linkgun_Projectile"));
	if (FlightEffectFinder.Succeeded())
	{
		FlightEffectComponent->SetTemplate(FlightEffectFinder.Object);
	}
	static ConstructorHelpers::FObjectFinder<UParticleSystem> ImpactEffectFinder(
		TEXT("/Game/RestrictedAssets/Weapons/LinkGun/Effects/P_WP_Linkgun_Impact"));
	ImpactEffect = ImpactEffectFinder.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> ImpactSoundFinder(
		TEXT("/Game/RestrictedAssets/Weapons/LinkGun/Assets/Audio/CUE/A_Weapon_Link_ImpactCue"));
	ImpactSound = ImpactSoundFinder.Object;

	if (ProjectileMovement != nullptr)
	{
		ProjectileMovement->InitialSpeed = 4000.0f;
		ProjectileMovement->MaxSpeed = 4000.0f;
		ProjectileMovement->ProjectileGravityScale = 1.0f;
		ProjectileMovement->bRotationFollowsVelocity = true;
		ProjectileMovement->bShouldBounce = false;
	}

	DamageParams.BaseDamage = 80.0f;
	DamageParams.MinimumDamage = 0.0f;
	DamageParams.InnerRadius = 0.0f;
	DamageParams.OuterRadius = 220.0f;
	DamageParams.DamageFalloff = 1.0f;
	Momentum = 40000.0f;
	MyDamageType = UUTDmgType_ScorpionGlob::StaticClass();
	InitialLifeSpan = 1.6f;
	bCanHitInstigator = false;
	bDamageOnBounce = true;
	bReplicateUTMovement = true;
}

void AUTProj_ScorpionGlob::Explode_Implementation(const FVector& HitLocation,
	const FVector& HitNormal, UPrimitiveComponent* HitComp)
{
	if (!bExploded && GetWorld() != nullptr && GetWorld()->GetNetMode() != NM_DedicatedServer)
	{
		if (ImpactEffect != nullptr)
		{
			UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), ImpactEffect, HitLocation,
				HitNormal.Rotation(), true);
		}
		if (ImpactSound != nullptr)
		{
			UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, HitLocation);
		}
	}
	Super::Explode_Implementation(HitLocation, HitNormal, HitComp);
}
