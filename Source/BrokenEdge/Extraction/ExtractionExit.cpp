#include "ExtractionExit.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

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
	const FVector D = GetActorTransform().InverseTransformPosition(Position) - Definition.BoundsOffset;
	return FMath::Abs(D.X) <= Definition.HalfExtent.X && FMath::Abs(D.Y) <= Definition.HalfExtent.Y
		&& FMath::Abs(D.Z) <= Definition.HalfExtent.Z;
}

void AExtractionExit::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionExit, Definition);
	DOREPLIFETIME(AExtractionExit, RoundStartedAt);
	DOREPLIFETIME(AExtractionExit, UsedCapacity);
}

void AExtractionExit::Configure(const FExtractionExitDefinition& InDefinition)
{
	if (!HasAuthority() || !InDefinition.IsValid()) return;
	Definition = InDefinition;
	SetActorTransform(Definition.Transform);
	OnRep_Definition();
	ForceNetUpdate();
}

void AExtractionExit::OnRep_Definition()
{
	Marker->SetRelativeScale3D(FVector(Definition.HalfExtent.X / 50, Definition.HalfExtent.Y / 50, .03));
	Label->SetText(FText::Format(NSLOCTEXT("Extraction", "ExitLabel", "{0}\n{1} s"),
		Definition.Label, FText::AsNumber(Definition.HoldSeconds)));
}

void AExtractionExit::StartRound(double ServerTime)
{
	if (!HasAuthority()) return;
	RoundStartedAt = ServerTime;
	UsedCapacity = 0;
	ForceNetUpdate();
}

FString AExtractionExit::UnavailableReason(double ServerTime) const
{
	if (!Definition.IsValid() || RoundStartedAt < 0) return TEXT("尚未开放");
	if (Definition.Capacity > 0 && UsedCapacity >= Definition.Capacity) return TEXT("名额已用尽");
	const double Elapsed = ServerTime - RoundStartedAt;
	if (Elapsed < Definition.OpensAfterSeconds)
		return FString::Printf(TEXT("%.0f 秒后开放"), FMath::CeilToDouble(Definition.OpensAfterSeconds - Elapsed));
	if (Definition.ClosesAfterSeconds > 0 && Elapsed >= Definition.ClosesAfterSeconds) return TEXT("已关闭");
	return {};
}

bool AExtractionExit::Available(double ServerTime) const { return UnavailableReason(ServerTime).IsEmpty(); }

bool AExtractionExit::Consume()
{
	if (!HasAuthority() || !Available(GetWorld()->GetTimeSeconds())) return false;
	++UsedCapacity;
	ForceNetUpdate();
	return true;
}
