#include "ExtractionExit.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInterface.h"

AExtractionExit::AExtractionExit()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
	Marker->SetupAttachment(RootComponent);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cube"));
	if (Mesh.Succeeded()) Marker->SetStaticMesh(Mesh.Object);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Game/Extraction/Materials/M_ExtractionExit"));
	if (Material.Succeeded()) Marker->SetMaterial(0, Material.Object);
	Marker->SetRelativeScale3D(FVector(5, 5, .03));
	Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marker->SetCastShadow(false);
	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(RootComponent);
	Label->SetAbsolute(false, false, true);
	Label->SetRelativeLocation(FVector(0, 0, 100));
	Label->SetRelativeRotation(FRotator(0, 90, 0));
	Label->SetText(FText::FromString(TEXT("EXTRACTION\n10 s")));
	Label->SetWorldSize(45);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetTextRenderColor(FColor(70, 230, 170));
}

bool AExtractionExit::Contains(const FVector& Position) const
{
	const FVector D = Position - GetActorLocation();
	return FMath::Abs(D.X) <= 250 && FMath::Abs(D.Y) <= 250 && D.Z >= -15 && D.Z <= 250;
}
