#include "TraversalAcceptanceFixture.h"
#include "BrokenEdge/Traversal/ExtractionLadder.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ATraversalAcceptanceFixture::ATraversalAcceptanceFixture()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	const auto Box = [this](const TCHAR* Name, FVector Location, FVector Size)
	{
		auto* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Mesh->SetupAttachment(RootComponent);
		Mesh->SetStaticMesh(Cube.Object);
		Mesh->SetRelativeLocation(Location);
		Mesh->SetRelativeScale3D(Size / 100.f);
		Mesh->SetMobility(EComponentMobility::Static);
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	};
	Box(TEXT("Floor"), FVector(0, 0, -20), FVector(3200, 5000, 40));
	Box(TEXT("Vault60"), FVector(0, 0, 30), FVector(60, 220, 60));
	Box(TEXT("Vault90"), FVector(0, 350, 45), FVector(60, 220, 90));
	Box(TEXT("Vault120"), FVector(0, 700, 60), FVector(60, 220, 120));
	Box(TEXT("Mantle180"), FVector(150, 1050, 90), FVector(400, 220, 180));
	Box(TEXT("TooHigh"), FVector(150, 1400, 110), FVector(400, 220, 220));
	Box(TEXT("Mantle121"), FVector(700, 0, 60.5f), FVector(400, 220, 121));
	Box(TEXT("TooHigh181"), FVector(700, 350, 90.5f), FVector(400, 220, 181));
	Box(TEXT("Narrow"), FVector(150, 1750, 90), FVector(400, 35, 180));
	Box(TEXT("LowObstacle"), FVector(0, -350, 45), FVector(60, 220, 90));
	Box(TEXT("LowCeiling"), FVector(0, -350, 220), FVector(350, 240, 20));
	Box(TEXT("LadderPlatform"), FVector(110, -900, 125), FVector(200, 240, 250));
	for (int32 I = 0; I < 10; ++I)
	{
		const FString Name = FString::Printf(TEXT("Rung%d"), I);
		Box(*Name, FVector(-5, -900, 15 + I * 25), FVector(5, 75, 3));
	}
	Box(TEXT("LadderLeftRail"), FVector(-5, -937, 125), FVector(5, 5, 250));
	Box(TEXT("LadderRightRail"), FVector(-5, -863, 125), FVector(5, 5, 250));
	for (int32 I = 0; I < 10; ++I)
	{
		const FName Name(*FString::Printf(TEXT("Step%d"), I));
		Box(*Name.ToString(), FVector(-600 + I * 40, -1500, 10 + I * 10), FVector(40, 250, 20 + I * 20));
	}
}

void ATraversalAcceptanceFixture::BeginPlay()
{
	Super::BeginPlay();
#if WITH_DEV_AUTOMATION_TESTS
	if (HasAuthority())
	{
		auto* Ladder = GetWorld()->SpawnActor<AExtractionLadder>(GetActorLocation() + FVector(-65, -900, 0),
		                                                         FRotator::ZeroRotator);
		Ladder->SourceId = TEXT("automation_fixture");
		Ladder->TopExit = FVector(150, 0, 250);
		for (int32 I = 0; I < 10; ++I) Ladder->Rungs.Add(FVector(60, 0, 16.5f + I * 25));
		Ladder->SetOwner(this);
	}
#endif
}
