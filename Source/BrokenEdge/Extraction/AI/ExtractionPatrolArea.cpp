#include "BrokenEdge/Extraction/AI/ExtractionPatrolArea.h"

#include "Components/BoxComponent.h"

AExtractionPatrolArea::AExtractionPatrolArea()
{
	PrimaryActorTick.bCanEverTick = false;
	Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("PatrolBounds"));
	SetRootComponent(Bounds);
	Bounds->SetBoxExtent(FVector(1000, 1000, 250));
	Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bounds->SetCanEverAffectNavigation(false);
	Bounds->SetHiddenInGame(true);
	Bounds->ShapeColor = FColor(50, 220, 120);
}

bool AExtractionPatrolArea::ContainsPatrolLocation(const FVector& Location, float HorizontalInset) const
{
	if (!IsValid(Bounds) || Location.ContainsNaN() || !FMath::IsFinite(HorizontalInset)) return false;
	const FTransform Transform = Bounds->GetComponentTransform();
	const FVector Scale = Transform.GetScale3D().GetAbs();
	if (Transform.ContainsNaN() || Scale.GetMin() <= UE_SMALL_NUMBER) return false;
	const FVector Local = Transform.InverseTransformPosition(Location);
	const FVector Extent = Bounds->GetUnscaledBoxExtent();
	// 将世界单位的水平内缩换算成局部单位，兼容非均匀缩放。零/负尺寸不会被当作有效区域。
	const double X = Extent.X - FMath::Max(0.f, HorizontalInset) / Scale.X;
	const double Y = Extent.Y - FMath::Max(0.f, HorizontalInset) / Scale.Y;
	return X > 0 && Y > 0 && Extent.Z > 0 && FMath::Abs(Local.X) <= X
		&& FMath::Abs(Local.Y) <= Y && FMath::Abs(Local.Z) <= Extent.Z;
}

FVector AExtractionPatrolArea::SamplePatrolLocation() const
{
	const FVector Extent = Bounds->GetUnscaledBoxExtent();
	return Bounds->GetComponentTransform().TransformPosition(
		FVector(FMath::FRandRange(-Extent.X, Extent.X), FMath::FRandRange(-Extent.Y, Extent.Y), 0));
}
