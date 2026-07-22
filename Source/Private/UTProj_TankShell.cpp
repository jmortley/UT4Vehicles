#include "UTProj_TankShell.h"
#include "UTVehicleDamageType.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "UObject/ConstructorHelpers.h"

AUTProj_TankShell::AUTProj_TankShell(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (CollisionComp != nullptr)
	{
		CollisionComp->InitSphereRadius(18.0f);
	}
	OverlapRadius = 22.0f;

	ShellMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShellMesh"));
	ShellMesh->SetupAttachment(RootComponent);
	ShellMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShellMesh->SetRelativeScale3D(FVector(0.12f, 0.06f, 0.06f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ShellMeshFinder(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (ShellMeshFinder.Succeeded())
	{
		ShellMesh->SetStaticMesh(ShellMeshFinder.Object);
	}

	FlightEffectComponent = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("FlightEffect"));
	FlightEffectComponent->SetupAttachment(RootComponent);
	FlightEffectComponent->bAutoActivate = true;
	static ConstructorHelpers::FObjectFinder<UParticleSystem> FlightEffectFinder(
		TEXT("/Game/RestrictedAssets/Weapons/RocketLauncher/P_RocketGrenade_Trail"));
	if (FlightEffectFinder.Succeeded())
	{
		FlightEffectComponent->SetTemplate(FlightEffectFinder.Object);
	}
	static ConstructorHelpers::FObjectFinder<UParticleSystem> ImpactEffectFinder(
		TEXT("/Game/RestrictedAssets/Weapons/RocketLauncher/VFX/Particles/P_Rocket_Explo_Base"));
	ImpactEffect = ImpactEffectFinder.Object;

	if (ProjectileMovement != nullptr)
	{
		ProjectileMovement->InitialSpeed = 15000.0f;
		ProjectileMovement->MaxSpeed = 15000.0f;
		ProjectileMovement->ProjectileGravityScale = 0.0f;
		ProjectileMovement->bRotationFollowsVelocity = true;
		ProjectileMovement->bShouldBounce = false;
	}

	// Deliberate UT4 balance override: the UT3 shell was 360. A 340-point
	// center hit lets a fully stacked 399-effective-health player barely live.
	DamageParams.BaseDamage = 340.0f;
	DamageParams.MinimumDamage = 0.0f;
	DamageParams.InnerRadius = 0.0f;
	DamageParams.OuterRadius = 600.0f;
	DamageParams.DamageFalloff = 1.0f;
	Momentum = 150000.0f;
	MyDamageType = UUTDmgType_TankShell::StaticClass();
	InitialLifeSpan = 1.2f;
	bCanHitInstigator = false;
	bDamageOnBounce = true;
	bReplicateUTMovement = true;
}

void AUTProj_TankShell::Explode_Implementation(const FVector& HitLocation,
	const FVector& HitNormal, UPrimitiveComponent* HitComp)
{
	if (!bExploded && GetWorld() != nullptr && GetWorld()->GetNetMode() != NM_DedicatedServer &&
		ImpactEffect != nullptr)
	{
		UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), ImpactEffect, HitLocation,
			HitNormal.Rotation(), true);
	}
	Super::Explode_Implementation(HitLocation, HitNormal, HitComp);
}
