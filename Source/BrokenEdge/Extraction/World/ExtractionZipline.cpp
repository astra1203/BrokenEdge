#include "BrokenEdge/Extraction/World/ExtractionZipline.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AExtractionZipline::AExtractionZipline()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Metal(
		TEXT("/Game/Extraction/Loot/FlightCase/M_FlightCase_Metal"));
	auto Make = [&](const TCHAR* Name)
	{
		auto* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(RootComponent);
		if (Cylinder.Succeeded()) Part->SetStaticMesh(Cylinder.Object);
		if (Metal.Succeeded()) Part->SetMaterial(0, Metal.Object);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		return Part;
	};
	TopPost = Make(TEXT("TopPost"));
	BottomPost = Make(TEXT("BottomPost"));
	Cable = Make(TEXT("Cable"));
	Cable->SetCastShadow(false);
	TopLabel = NSLOCTEXT("Extraction", "ZiplineTop", "上端");
	BottomLabel = NSLOCTEXT("Extraction", "ZiplineBottom", "下端");
}

void AExtractionZipline::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyLayout();
}

// 引擎圆柱直径 100、高 100、轴心在中心、沿 Z 轴。
void AExtractionZipline::ApplyLayout()
{
	for (const auto& [Post, Anchor] : {TPair<UStaticMeshComponent*, FVector>(TopPost, TopAnchor),
	                                   TPair<UStaticMeshComponent*, FVector>(BottomPost, BottomAnchor)})
	{
		Post->SetRelativeLocation(Anchor - FVector(0, 0, PostHeight * .5f - 10.f));
		Post->SetRelativeRotation(FRotator::ZeroRotator);
		Post->SetRelativeScale3D(FVector(.14f, .14f, PostHeight / 100.f));
	}
	const FVector Span = BottomAnchor - TopAnchor;
	Cable->SetRelativeLocation((TopAnchor + BottomAnchor) * .5f);
	Cable->SetRelativeRotation(FRotationMatrix::MakeFromZ(Span.GetSafeNormal()).Rotator());
	Cable->SetRelativeScale3D(FVector(.025f, .025f, Span.Size() / 100.f));
}

bool AExtractionZipline::CanReach(const ABrokenEdgeCharacter* Character, bool& bFromTop) const
{
	if (!IsValid(Character) || Character->IsOnZipline()) return false;
	const FVector At = Character->GetActorLocation();
	for (const bool bTop : {true, false})
	{
		// 以立柱脚下站立的胶囊中心为准（柱脚 + 约 90 cm），和立柱多高无关。
		const FVector Stand = (bTop ? TopWorld() : BottomWorld()) - FVector(0, 0, PostHeight - 90.f);
		if (FVector::Dist2D(At, Stand) <= 220.f && FMath::Abs(At.Z - Stand.Z) <= 160.f)
		{
			bFromTop = bTop;
			return true;
		}
	}
	return false;
}

FString AExtractionZipline::PromptText(const ABrokenEdgeCharacter* Character) const
{
	bool bFromTop = false;
	if (!CanReach(Character, bFromTop)) return FString();
	return TEXT("F  滑索 · 到") + (bFromTop ? BottomLabel : TopLabel).ToString() + TEXT("（空格松手）");
}

FString AExtractionZipline::Use(ABrokenEdgeCharacter* Character)
{
	if (!HasAuthority() || !IsValid(Character)) return FString();
	bool bFromTop = false;
	if (!CanReach(Character, bFromTop)) return TEXT("离滑索太远");
	if (Character->IsProne()) return TEXT("趴着不能挂滑索");
	const FVector From = (bFromTop ? TopWorld() : BottomWorld()) - FVector(0, 0, HangDrop);
	const FVector To = (bFromTop ? BottomWorld() : TopWorld()) - FVector(0, 0, HangDrop);
	Character->StartZipline(this, From, To);
	return FString();
}

void AExtractionZipline::BeginPlay()
{
	Super::BeginPlay();
	ApplyLayout();
}
