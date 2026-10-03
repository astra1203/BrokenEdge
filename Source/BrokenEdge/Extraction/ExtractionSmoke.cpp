#include "ExtractionSmoke.h"
#include "ExtractionAbilities.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"

AExtractionSmoke::AExtractionSmoke()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	Cloud = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SmokeCloud"));
	SetRootComponent(Cloud);
	Cloud->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cloud->SetCanEverAffectNavigation(false);
	Cloud->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
	if (Sphere.Succeeded()) Cloud->SetStaticMesh(Sphere.Object);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Game/Extraction/Abilities/M_ExtractionSmoke"));
	if (Material.Succeeded()) Cloud->SetMaterial(0, Material.Object);
}

void AExtractionSmoke::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Radius = GetDefault<UExtractionRules>()->SmokeRadius;
		ExpiresAt = GetWorld()->GetTimeSeconds() + GetDefault<UExtractionRules>()->SmokeSeconds;
		SetLifeSpan(GetDefault<UExtractionRules>()->SmokeSeconds);
	}
	Refresh();
}

void AExtractionSmoke::Refresh() { Cloud->SetWorldScale3D(FVector(Radius / 50.f)); }

void AExtractionSmoke::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionSmoke, Radius);
	DOREPLIFETIME(AExtractionSmoke, ExpiresAt);
}

bool AExtractionSmoke::Blocks(const UWorld* World, const FVector& Start, const FVector& End)
{
	if (!World) return false;
	const auto* GS = World->GetGameState();
	const double Now = GS ? GS->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	for (TActorIterator<AExtractionSmoke> It(World); It; ++It)
		if (Now < It->ExpiresAt && FMath::PointDistToSegmentSquared(It->GetActorLocation(), Start, End) <
			FMath::Square(It->Radius)) return true;
	return false;
}
