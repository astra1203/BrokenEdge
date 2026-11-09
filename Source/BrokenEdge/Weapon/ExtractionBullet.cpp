#include "ExtractionBullet.h"
#include "BrokenEdge/Extraction/ExtractionBreakableGlass.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/LagCompensationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "BrokenEdge/Extraction/ExtractionHealth.h"
#include "BrokenEdge/Extraction/ExtractionItems.h"
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

static TAutoConsoleVariable<float> CVarPenetrationPerLevel(
	TEXT("BrokenEdge.Weapon.PenetrationCmPerLevel"), 4.f,
	TEXT("Max static obstacle thickness (cm) a bullet passes per ammo penetration level; 0 disables penetration."));

static TAutoConsoleVariable<int32> CVarWeaponFlightLog(
	TEXT("BrokenEdge.Weapon.LogFlight"), 0,
	TEXT("Log authoritative bullet initialization and first impact; no per-tick logging."));

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
	       TEXT(
		       "EXTRACTION_BULLET_INIT Bullet=%s Owner=%s Instigator=%s Start=%s Direction=%s Velocity=%s Speed=%.3f BodyDamage=%.3f HeadDamage=%.3f Radius=%.3f"
	       ),
	       *GetName(), *GetNameSafe(GetOwner()), *GetNameSafe(GetInstigator()), *GetActorLocation().ToString(),
	       *Direction.GetSafeNormal().ToString(), *Velocity.ToString(), Stats.Speed, BodyDamage, HeadDamage,
	       Stats.Radius);
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
	// 延迟补偿：有历史记录的角色从主扫掠中排除，改为对其倒带后的物理资产姿态单独检测。
	const bool bRewind = HasAuthority() && !bPredicted && RewindSeconds > 0;
	TArray<const AActor*> RewindIgnore;
	if (bRewind)
	{
		TArray<AActor*> RewindActors;
		ULagCompensationComponent::GetRewindActors(GetWorld(), RewindActors);
		Query.AddIgnoredActors(RewindActors);
		RewindIgnore.Add(GetInstigator());
	}
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
		FRewindHit Rewound;
		// 本步结束时刻（尚未模拟的时间留在 Accumulator 里），再减去射手的延迟。
		if (bRewind && ULagCompensationComponent::TraceWorldAt(
			GetWorld(), GetWorld()->GetTimeSeconds() - Accumulator - RewindSeconds, Start,
			Hit.bBlockingHit ? Hit.Location : End, Stats.Radius, RewindIgnore, Rewound))
		{
			// 倒带姿态比世界里的阻挡更近：改写为命中该角色的网格，后续伤害与表现走同一条路径。
			Hit = FHitResult(Rewound.Character, Rewound.Character->GetMesh(), Rewound.Location,
			                 -Velocity.GetSafeNormal());
			Hit.bBlockingHit = true;
			Hit.TraceStart = Start;
			Hit.TraceEnd = End;
			Hit.Time = Rewound.Fraction;
			Hit.BoneName = Rewound.Bone;
			if (CVarWeaponFlightLog.GetValueOnGameThread())
				UE_LOG(LogTemp, Display, TEXT("EXTRACTION_BULLET_REWIND Bullet=%s Victim=%s Bone=%s Rewind=%.3f"),
				       *GetName(), *GetNameSafe(Rewound.Character), *Rewound.Bone.ToString(), RewindSeconds);
		}
		const FVector Finish = Hit.bBlockingHit ? Hit.Location : End;
		Distance += FVector::Distance(Start, Finish);
		SetActorLocation(Finish);
		if (CVarWeaponFlight.GetValueOnGameThread()) DrawDebugLine(GetWorld(), Start, Finish, FColor::Yellow, false, 3);
		FVector Exit;
		if (auto* Glass = Cast<AExtractionBreakableGlass>(Hit.GetActor()); Hit.bBlockingHit && Glass
			&& (bPredicted || (HasAuthority() && Glass->Break(Hit.ImpactPoint, Velocity))))
		{
			Query.AddIgnoredActor(Glass);
			SetActorLocation(Hit.ImpactPoint + Velocity.GetSafeNormal() * (Stats.Radius + 1.f));
			continue;
		}
		if (Hit.bBlockingHit && TryPenetrate(Hit, Exit))
		{
			if (HasAuthority() && !bPredicted)
			{
				Impact(Hit.ImpactPoint, Hit.ImpactNormal);
				Impact(Exit, Velocity.GetSafeNormal());
			}
			Distance += FVector::Distance(Finish, Exit);
			SetActorLocation(Exit);
			continue;
		}
		if (Hit.bBlockingHit)
		{
			if (HasAuthority() && !bPredicted && CVarWeaponFlightLog.GetValueOnGameThread())
				UE_LOG(LogTemp, Display,
			       TEXT(
				       "EXTRACTION_BULLET_IMPACT Bullet=%s Owner=%s Instigator=%s Actor=%s Component=%s ComponentPath=%s Bone=%s Location=%s ImpactPoint=%s Normal=%s ImpactNormal=%s StartPenetrating=%d Distance=%.3f BodyDamage=%.3f"
			       ),
			       *GetName(), *GetNameSafe(GetOwner()), *GetNameSafe(GetInstigator()), *GetNameSafe(Hit.GetActor()),
			       *GetNameSafe(Hit.GetComponent()), *GetPathNameSafe(Hit.GetComponent()), *Hit.BoneName.ToString(),
			       *Hit.Location.ToString(), *Hit.ImpactPoint.ToString(), *Hit.Normal.ToString(),
			       *Hit.ImpactNormal.ToString(), Hit.bStartPenetrating ? 1 : 0, Distance, BodyDamage);
			if (auto* Victim = Cast<ABrokenEdgeCharacter>(Hit.GetActor()); Victim && HasAuthority() && !bPredicted)
				Victim->GetExtractionHealth()->Bullet(
					(Hit.BoneName == TEXT("head") ? HeadDamage : BodyDamage) * Stats.Falloff(Distance) *
					PenetrationScale,
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

bool AExtractionBullet::TryPenetrate(const FHitResult& Hit, FVector& Exit)
{
	UPrimitiveComponent* Component = Hit.GetComponent();
	if (bPenetrated || !Component || Hit.bStartPenetrating || Cast<APawn>(Hit.GetActor())) return false;
	const auto* Ammo = UExtractionItemDefinition::Get(AmmoType);
	const float MaxThickness = CVarPenetrationPerLevel.GetValueOnGameThread() * (Ammo ? Ammo->Penetration : 1);
	if (MaxThickness <= 0) return false;
	// 从最大厚度外侧反向探测同一组件：命中即为背面；探测不到说明物体比上限更厚（或远端仍在实体内）。
	const FVector Direction = Velocity.GetSafeNormal();
	const FVector Far = Hit.ImpactPoint + Direction * MaxThickness;
	FHitResult Back;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ExtractionBulletPenetration), true);
	if (!Component->LineTraceComponent(Back, Far, Hit.ImpactPoint + Direction * .5f, Query) || Back.bStartPenetrating)
		return false;
	const float Thickness = FVector::Distance(Hit.ImpactPoint, Back.ImpactPoint);
	// 出射后离开背面一个弹丸半径，避免下一步扫掠从墙体内部开始。
	Exit = Back.ImpactPoint + Direction * (Stats.Radius + 1.f);
	bPenetrated = true;
	PenetrationScale = FMath::Lerp(.75f, .35f, FMath::Clamp(Thickness / MaxThickness, 0.f, 1.f));
	Velocity *= FMath::Sqrt(PenetrationScale);
	if (HasAuthority() && !bPredicted && CVarWeaponFlightLog.GetValueOnGameThread())
		UE_LOG(LogTemp, Display, TEXT("EXTRACTION_BULLET_PENETRATE Bullet=%s Actor=%s Thickness=%.2f Max=%.2f Scale=%.2f"),
	       *GetName(), *GetNameSafe(Hit.GetActor()), Thickness, MaxThickness, PenetrationScale);
	return true;
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
