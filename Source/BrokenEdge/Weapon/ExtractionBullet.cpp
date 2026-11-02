#include "ExtractionBullet.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Extraction/ExtractionHealth.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Sound/SoundCue.h"

static TAutoConsoleVariable<int32> CVarWeaponFlight(
	TEXT("BrokenEdge.Weapon.DebugFlight"), 0, TEXT("Draw authoritative swept bullet paths."));

static TAutoConsoleVariable<int32> CVarWeaponFlightLog(
	TEXT("BrokenEdge.Weapon.LogFlight"), 0, TEXT("Log authoritative bullet initialization and first impact; no per-tick logging."));

AExtractionBullet::AExtractionBullet()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	bReplicates = true;
	SetReplicateMovement(true);
	PrimaryActorTick.bCanEverTick = true;
	InitialLifeSpan = 8;
}

void AExtractionBullet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionBullet, ImpactEffect);
	DOREPLIFETIME(AExtractionBullet, ImpactSound);
	DOREPLIFETIME(AExtractionBullet, TrailEffect);
}

void AExtractionBullet::Initialize(const FResolvedWeaponStats& Profile, const FVector& Direction, float Damage,
                                   float InHeadDamage, FName Ammo, UParticleSystem* InImpact, USoundCue* Sound,
                                   UParticleSystem* Trail)
{
	Stats = Profile;
	Velocity = Direction.GetSafeNormal() * Stats.Speed;
	BodyDamage = Damage;
	HeadDamage = InHeadDamage;
	AmmoType = Ammo;
	ImpactEffect = InImpact;
	ImpactSound = Sound;
	TrailEffect = Trail;
	PreviousVisual = GetActorLocation();
	bPredicted = GetNetMode() == NM_Client;
	if (HasAuthority() && !bPredicted && CVarWeaponFlightLog.GetValueOnGameThread())
		UE_LOG(LogTemp, Display,
		       TEXT("EXTRACTION_BULLET_INIT Bullet=%s Owner=%s Instigator=%s Start=%s Direction=%s Velocity=%s Speed=%.3f BodyDamage=%.3f HeadDamage=%.3f Radius=%.3f"),
		       *GetName(), *GetNameSafe(GetOwner()), *GetNameSafe(GetInstigator()), *GetActorLocation().ToString(),
		       *Direction.GetSafeNormal().ToString(), *Velocity.ToString(), Stats.Speed, BodyDamage, HeadDamage, Stats.Radius);
}

void AExtractionBullet::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if ((!HasAuthority() && !bPredicted) || !Stats.IsValid())
	{
		UpdateTrail();
		return;
	}
	Accumulator += DeltaSeconds;
	constexpr float Step = 1.f / 120.f;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ExtractionBullet), true, this);
	Query.AddIgnoredActor(GetOwner());
	Query.AddIgnoredActor(GetInstigator());
	// Retain time debt through a hitch, rather than increasing bullet speed or dropping travel.
	for (int32 Work = 0; Accumulator >= Step && Work < 240; ++Work)
	{
		Accumulator -= Step;
		const FVector Start = GetActorLocation();
		const float Gravity = Distance >= Stats.GravityFreeDistance ? Stats.Gravity : 0;
		const FVector End = Start + Velocity * Step - FVector(0, 0, .5f * Gravity * Step * Step);
		Velocity.Z -= Gravity * Step;
		FHitResult Hit;
		GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility,
		                                 FCollisionShape::MakeSphere(Stats.Radius), Query);
		const FVector Finish = Hit.bBlockingHit ? Hit.Location : End;
		Distance += FVector::Distance(Start, Finish);
		SetActorLocation(Finish);
		if (CVarWeaponFlight.GetValueOnGameThread()) DrawDebugLine(GetWorld(), Start, Finish, FColor::Yellow, false, 3);
		if (Hit.bBlockingHit)
		{
			if (HasAuthority() && !bPredicted && CVarWeaponFlightLog.GetValueOnGameThread())
				UE_LOG(LogTemp, Display,
				       TEXT("EXTRACTION_BULLET_IMPACT Bullet=%s Owner=%s Instigator=%s Actor=%s Component=%s ComponentPath=%s Bone=%s Location=%s ImpactPoint=%s Normal=%s ImpactNormal=%s StartPenetrating=%d Distance=%.3f BodyDamage=%.3f"),
				       *GetName(), *GetNameSafe(GetOwner()), *GetNameSafe(GetInstigator()), *GetNameSafe(Hit.GetActor()),
				       *GetNameSafe(Hit.GetComponent()), *GetPathNameSafe(Hit.GetComponent()), *Hit.BoneName.ToString(),
				       *Hit.Location.ToString(), *Hit.ImpactPoint.ToString(), *Hit.Normal.ToString(),
				       *Hit.ImpactNormal.ToString(), Hit.bStartPenetrating ? 1 : 0, Distance, BodyDamage);
			if (auto* Victim = Cast<ABrokenEdgeCharacter>(Hit.GetActor()); Victim && HasAuthority() && !bPredicted)
				Victim->GetExtractionHealth()->Bullet(
					(Hit.BoneName == TEXT("head") ? HeadDamage : BodyDamage) * Stats.Falloff(Distance),
					Hit.BoneName, AmmoType, GetInstigatorController(), this);
			if (HasAuthority() && !bPredicted) Impact(Hit.ImpactPoint, Hit.ImpactNormal);
			UpdateTrail();
			if (TrailComponent) TrailComponent->DeactivateSystem();
			SetActorTickEnabled(false);
			SetLifeSpan(.25f);
			return;
		}
		if (Distance >= Stats.MaxDistance)
		{
			Destroy();
			return;
		}
	}
	UpdateTrail();
}

void AExtractionBullet::Impact_Implementation(FVector_NetQuantize Point, FVector_NetQuantizeNormal Normal)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	if (CVarWeaponFlight.GetValueOnGameThread())
		DrawDebugPoint(GetWorld(), Point, 10.f, FColor::Red, false, 30.f);
	if (ImpactEffect) UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), ImpactEffect, Point, Normal.Rotation());
	if (ImpactSound) UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, Point);
}

void AExtractionBullet::UpdateTrail()
{
	const bool OwnerView = GetInstigator() && GetInstigator()->IsLocallyControlled();
	if (GetNetMode() != NM_DedicatedServer && TrailEffect && (HasAuthority() || bPredicted || !OwnerView))
	{
		if (!TrailComponent) TrailComponent = UGameplayStatics::SpawnEmitterAttached(TrailEffect, RootComponent);
		if (TrailComponent)
		{
			TrailComponent->SetWorldLocation(PreviousVisual.IsNearlyZero() ? GetActorLocation() : PreviousVisual);
			TrailComponent->SetVectorParameter(TEXT("Target"), GetActorLocation());
		}
		PreviousVisual = GetActorLocation();
	}
}
