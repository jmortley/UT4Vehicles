#include "UTProj_TankShell.h"
#include "UnrealTournament.h"
#include "UTVehicleDamageType.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/ProjectileMovementComponent.h"

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

	if (ProjectileMovement != nullptr)
	{
		ProjectileMovement->InitialSpeed = 15000.0f;
		ProjectileMovement->MaxSpeed = 15000.0f;
		ProjectileMovement->ProjectileGravityScale = 0.0f;
		ProjectileMovement->bRotationFollowsVelocity = true;
		ProjectileMovement->bShouldBounce = false;
	}

	DamageParams.BaseDamage = 360.0f;
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
