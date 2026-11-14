#include "BrokenEdge/Extraction/World/ExtractionBreakableGlass.h"
#include "BrokenEdge/Extraction/Core/ExtractionState.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/DamageEvents.h"
#include "Particles/ParticleSystemComponent.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"

AExtractionBreakableGlass::AExtractionBreakableGlass()
{
	PrimaryActorTick.bCanEverTick = false;
	Pane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pane"));
	SetRootComponent(Pane);
	Pane->SetMobility(EComponentMobility::Movable);
	Pane->SetCollisionProfileName(TEXT("BlockAll"));
	Pane->SetCanEverAffectNavigation(false);
}

void AExtractionBreakableGlass::BeginPlay()
{
	Super::BeginPlay();
	SuppressLegacyPane();
	if (const auto* State = GetWorld()->GetGameState<AExtractionGameState>(); State && State->BrokenGlass.
		Contains(PaneId))
		ApplyBroken();
}

void AExtractionBreakableGlass::SuppressLegacyPane()
{
	if (!bReplacesLegacyWindow) return;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		TInlineComponentArray<UStaticMeshComponent*> Components(*It);
		for (auto* Component : Components)
			if (Component->GetName().EndsWith(TEXT("_StaticGlass"))
				&& Component->GetStaticMesh() == Pane->GetStaticMesh()
				&& Component->GetComponentTransform().Equals(Pane->GetComponentTransform(), .01f))
			{
				Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Component->SetVisibility(false);
				UE_LOG(LogTemp, Display, TEXT("EXTRACTION_GLASS_LEGACY_SUPPRESS Id=%s Component=%s"),
				       *PaneId.ToString(), *Component->GetPathName());
			}
	}
}

void AExtractionBreakableGlass::ApplyBroken()
{
	bBroken = true;
	Pane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Pane->SetVisibility(false);
}

bool AExtractionBreakableGlass::Break(const FVector& Impact, const FVector& Direction)
{
	auto* State = GetWorld()->GetGameState<AExtractionGameState>();
	if (!State || !State->HasAuthority() || !PaneId.IsValid()) return false;
	if (State->BrokenGlass.Contains(PaneId))
	{
		ApplyBroken();
		return true;
	}
	State->BrokenGlass.Add(PaneId);
	ApplyBroken();
	State->ForceNetUpdate();
	State->MulticastGlassBreak(PaneId, Impact, Direction.GetSafeNormal());
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_GLASS_BREAK Id=%s Location=%s"), *PaneId.ToString(), *Impact.ToString());
	return true;
}

float AExtractionBreakableGlass::TakeDamage(float Amount, const FDamageEvent& Event, AController* DamageInstigator,
                                            AActor* Causer)
{
	FVector Impact = Pane->Bounds.Origin, Direction = FVector::UpVector;
	if (Event.IsOfType(FPointDamageEvent::ClassID))
	{
		const auto& Point = static_cast<const FPointDamageEvent&>(Event);
		Impact = Point.HitInfo.ImpactPoint;
		Direction = Point.ShotDirection;
	}
	return Amount > 0 && Break(Impact, Direction) ? Amount : 0;
}

void AExtractionBreakableGlass::PlayBreakEffect(const FVector& Impact, const FVector& Direction)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	if (BreakSound) UGameplayStatics::PlaySoundAtLocation(this, BreakSound, Impact);
	// Local, short-lived shards cannot block or damage players and never replicate physics.
	if (auto* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube")))
		for (int32 Index = 0; Index < 8; ++Index)
		{
			FActorSpawnParameters Params;
			Params.ObjectFlags |= RF_Transient;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			auto* Shard = GetWorld()->SpawnActor<AStaticMeshActor>(Impact, FRotator::ZeroRotator, Params);
			if (!Shard) continue;
			auto* Body = Shard->GetStaticMeshComponent();
			Body->SetMobility(EComponentMobility::Movable);
			Body->SetStaticMesh(Mesh);
			Body->SetMaterial(0, Pane->GetMaterial(0));
			Body->SetWorldScale3D(FVector(FMath::FRandRange(.025f, .07f), FMath::FRandRange(.04f, .1f), .003f));
			Body->SetCanEverAffectNavigation(false);
			Body->SetCollisionResponseToAllChannels(ECR_Ignore);
			Body->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
			Body->SetSimulatePhysics(true);
			Body->SetPhysicsLinearVelocity(Direction.GetSafeNormal() * 160.f + FMath::VRand() * 100.f);
			Body->SetPhysicsAngularVelocityInDegrees(FMath::VRand() * 180.f);
			Shard->SetLifeSpan(2.f);
		}
	if (BreakEffect)
		if (auto* Effect = UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), BreakEffect, Impact,
		                                                            Direction.Rotation()))
			Effect->SetFloatParameter(TEXT("Lifetime"), 2.f);
}
