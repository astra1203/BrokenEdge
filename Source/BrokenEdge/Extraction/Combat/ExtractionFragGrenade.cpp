#include "BrokenEdge/Extraction/Combat/ExtractionFragGrenade.h"
#include "BrokenEdge/Extraction/World/ExtractionBreakableGlass.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionItems.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionLootActor.h"
#include "BrokenEdge/Extraction/Combat/ExtractionAbilities.h"
#include "BrokenEdge/Extraction/Health/ExtractionHealth.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	void IgnoreLooseLoot(UWorld* World, FCollisionQueryParams& Query)
	{
		// Pickup interaction volumes are not physical blast cover. Solid containers remain cover.
		for (TActorIterator<AExtractionLootActor> It(World); It; ++It)
			if (It->Loose) Query.AddIgnoredActor(*It);
	}
}

AExtractionFragGrenade::AExtractionFragGrenade()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);
	Shell = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shell"));
	SetRootComponent(Shell);
	// 与烟雾弹相同：只与世界几何碰撞，不被角色弹开。
	Shell->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Shell->SetCollisionResponseToAllChannels(ECR_Ignore);
	Shell->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Shell->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Shell->SetCollisionObjectType(ECC_WorldDynamic);
	Shell->SetCanEverAffectNavigation(false);
	Shell->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
	if (Sphere.Succeeded()) Shell->SetStaticMesh(Sphere.Object);
	Shell->SetRelativeScale3D(FVector(.1f));
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Shell);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCanEverAffectNavigation(false);
	Visual->SetRelativeScale3D(FVector(10.f));
	static ConstructorHelpers::FObjectFinder<UParticleSystem> Effect(
		TEXT("/Game/MilitaryWeapSilver/FX/P_Grenade_Explosion_01"));
	if (Effect.Succeeded()) ExplosionEffect = Effect.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> Sound(
		TEXT("/Game/MilitaryWeapSilver/Sound/GrenadeLauncher/Cues/GrenadeLauncher_Explosion_Cue"));
	if (Sound.Succeeded()) ExplosionSound = Sound.Object;
	Flight = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Flight"));
	Flight->UpdatedComponent = Shell;
	Flight->InitialSpeed = 0;
	Flight->MaxSpeed = 3000.f;
	Flight->ProjectileGravityScale = 1.f;
	// 金属弹体比烟雾弹更容易弹跳滚动。
	Flight->bShouldBounce = true;
	Flight->Bounciness = .35f;
	Flight->Friction = .6f;
	Flight->BounceVelocityStopSimulatingThreshold = 10.f;
}

void AExtractionFragGrenade::BeginPlay()
{
	Super::BeginPlay();
	if (const auto* Definition = UExtractionItemDefinition::Get(TEXT("FragGrenade")); Definition && Definition->
		WorldMesh)
	{
		Visual->SetStaticMesh(Definition->WorldMesh);
		Shell->SetVisibility(false);
	}
}

void AExtractionFragGrenade::Launch(const FVector& Velocity)
{
	if (!HasAuthority()) return;
	Flight->Velocity = Velocity;
	const float Fuse = FMath::Max(.1f, GetDefault<UExtractionRules>()->FragFuseSeconds);
	GetWorld()->GetTimerManager().SetTimer(FuseTimer, this, &AExtractionFragGrenade::Explode, Fuse, false);
}

bool AExtractionFragGrenade::Exposed(const ABrokenEdgeCharacter* Victim, const FVector& Origin) const
{
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ExtractionFragGrenade), false, this);
	Query.AddIgnoredActor(Victim);
	IgnoreLooseLoot(GetWorld(), Query);
	for (const FVector& Point : {Victim->GetActorLocation(), Victim->GetPawnViewLocation()})
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Origin, Point, ECC_Visibility, Query)) return true;
	}
	return false;
}

void AExtractionFragGrenade::Explode()
{
	if (!HasAuthority() || bExploded) return;
	bExploded = true;
	const auto* Rules = GetDefault<UExtractionRules>();
	// 爆点略抬离地面，避免贴地射线被地形本身挡住。
	const FVector Origin = GetActorLocation() + FVector(0, 0, 15);
	const float Radius = FMath::Max(1.f, Rules->FragRadius);
	const float Full = FMath::Clamp(Rules->FragFullRadius, 0.f, Radius);
	TArray<AExtractionBreakableGlass*> Panes;
	for (TActorIterator<AExtractionBreakableGlass> It(GetWorld()); It; ++It)
		if (!It->IsBroken() && It->Pane->Bounds.GetBox().ComputeSquaredDistanceToPoint(Origin) <= FMath::Square(Radius))
			Panes.Add(*It);
	Panes.Sort([&](const AExtractionBreakableGlass& A, const AExtractionBreakableGlass& B)
	{
		return A.Pane->Bounds.GetBox().ComputeSquaredDistanceToPoint(Origin) < B.Pane->Bounds.GetBox().
			ComputeSquaredDistanceToPoint(Origin);
	});
	for (auto* Glass : Panes)
	{
		const FVector Point = Glass->Pane->Bounds.Origin;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(ExtractionGlassExplosion), true, this);
		IgnoreLooseLoot(GetWorld(), Query);
		FHitResult Hit;
		const bool Blocked = GetWorld()->LineTraceSingleByChannel(Hit, Origin, Point, ECC_Visibility, Query);
		UE_LOG(LogTemp, Verbose,
		       TEXT("EXTRACTION_GLASS_BLAST Id=%s Origin=%s Target=%s Blocker=%s Component=%s Hit=%s"),
		       *Glass->PaneId.ToString(), *Origin.ToString(), *Point.ToString(), *GetNameSafe(Hit.GetActor()),
		       *GetNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToString());
		if (!Blocked || Hit.GetActor() == Glass)
			Glass->Break(Point, Point - Origin);
	}
	for (TActorIterator<ABrokenEdgeCharacter> It(GetWorld()); It; ++It)
	{
		auto* Victim = *It;
		if (Victim->IsDefeated()) continue;
		const float Distance = FVector::Dist(Origin, Victim->GetActorLocation());
		if (Distance > Radius || !Exposed(Victim, Origin)) continue;
		const float Alpha = Distance <= Full ? 0.f : (Distance - Full) / FMath::Max(1.f, Radius - Full);
		const float Damage = FMath::Lerp(Rules->FragDamage, Rules->FragMinDamage, Alpha);
		Victim->GetExtractionHealth()->Explosion(Damage, GetInstigatorController(), this);
	}
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_FRAG_EXPLODE At=%s Instigator=%s"), *Origin.ToCompactString(),
	       *GetNameSafe(GetInstigator()));
	MulticastExplode(Origin);
	SetLifeSpan(.2f);
}

void AExtractionFragGrenade::MulticastExplode_Implementation(FVector_NetQuantize Location)
{
	Shell->SetHiddenInGame(true, true);
	if (GetNetMode() == NM_DedicatedServer) return;
	if (ExplosionEffect) UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), ExplosionEffect, Location);
	if (ExplosionSound) UGameplayStatics::PlaySoundAtLocation(this, ExplosionSound, Location);
}
