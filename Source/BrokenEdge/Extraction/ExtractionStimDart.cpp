#include "ExtractionStimDart.h"
#include "ExtractionAbilities.h"
#include "ExtractionHealth.h"
#include "ExtractionState.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
// 激素枪飞镖：与枪械子弹相同的推进方式（固定 120 Hz 子步、球形扫掠、卡顿时保留时间债），
// 只是速度慢、无重力，命中后按友方规则施加激素而不是造成伤害。

AExtractionStimDart::AExtractionStimDart()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	// 弹体只做表现，碰撞由服务器扫掠负责，避免把角色或其他投射物推开。
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCanEverAffectNavigation(false);
	Body->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
	if (Sphere.Succeeded()) Body->SetStaticMesh(Sphere.Object);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Game/Extraction/Abilities/M_ExtractionSmokeShell"));
	if (Material.Succeeded()) Body->SetMaterial(0, Material.Object);
	// 基础球半径 50 cm，缩到约 2 cm，肉眼能看到一颗小飞镖飞过去。
	Body->SetRelativeScale3D(FVector(.04f));
	// 最远 100 m / 29 m/s ≈ 3.4 s，留余量后兜底销毁。
	InitialLifeSpan = 6;
}

void AExtractionStimDart::Launch(ABrokenEdgeCharacter* Shooter, const FVector& Direction)
{
	ShooterCharacter = Shooter;
	Velocity = Direction.GetSafeNormal() * FMath::Max(1.f, GetDefault<UExtractionRules>()->StimDartSpeed);
	SetActorRotation(Velocity.Rotation());
}

void AExtractionStimDart::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority()) return;
	const auto* R = GetDefault<UExtractionRules>();
	auto* Shooter = ShooterCharacter.Get();
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ExtractionStimDart), true, this);
	if (Shooter)
	{
		Query.AddIgnoredActor(Shooter);
		TArray<AActor*> Carried;
		Shooter->GetAttachedActors(Carried, true, true);
		Query.AddIgnoredActors(Carried);
	}
	Accumulator += DeltaSeconds;
	constexpr float Step = 1.f / 120.f;
	for (int32 Work = 0; Accumulator >= Step && Work < 240; ++Work)
	{
		Accumulator -= Step;
		const FVector Start = GetActorLocation();
		const FVector End = Start + Velocity * Step;
		FHitResult Hit;
		GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility,
		                                 FCollisionShape::MakeSphere(FMath::Max(.1f, R->StimDartRadius)), Query);
		const FVector Finish = Hit.bBlockingHit ? Hit.Location : End;
		Distance += FVector::Distance(Start, Finish);
		SetActorLocation(Finish);
		if (Hit.bBlockingHit)
		{
			Strike(Hit);
			Destroy();
			return;
		}
		if (Distance >= R->StimDartMaxDistance)
		{
			Destroy();
			return;
		}
	}
}

void AExtractionStimDart::Strike(const FHitResult& Hit)
{
	auto* Victim = Cast<ABrokenEdgeCharacter>(Hit.GetActor());
	auto* Shooter = ShooterCharacter.Get();
	const auto* PS = Victim ? Victim->GetPlayerState<AExtractionPlayerState>() : nullptr;
	// 官方作用对象为"队友和自己"，且倒地不吃激素（IncludeImpendingDeath=false）。
	const bool bApplied = PS && PS->Life == EExtractionLife::Alive && UExtractionAbilities::Friendly(Shooter, Victim);
	if (bApplied) Victim->GetExtractionHealth()->ApplyStim();
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_STIM_DART Shooter=%s Hit=%s Applied=%d Distance=%.0f"),
	       Shooter ? *Shooter->GetName() : TEXT("None"), Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("None"),
	       bApplied ? 1 : 0, Distance);
}
