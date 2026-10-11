#include "ExtractionLadder.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"

bool FTraversalLadderEntry::OverlapsCapsule(const FTransform& LadderTransform, const FVector& Center,
                                            float Radius, float HalfHeight) const
{
	if (Extent.GetMin() <= 0.f || Radius <= 0.f || HalfHeight < Radius) return false;
	const FTransform World = Transform * LadderTransform;
	// The restored ladders are upright. Reject unsupported tilted boxes instead of
	// silently using a vertical-capsule approximation on a different orientation.
	if (FMath::Abs(World.GetUnitAxis(EAxis::Z).Z) < .9999f) return false;
	const FVector P = World.InverseTransformPositionNoScale(Center).GetAbs();
	const FVector Size = Extent * World.GetScale3D().GetAbs();
	// Distance between the capsule's central vertical segment and the oriented box.
	// Checking only Center would miss the original 60 cm-high bottom entry boxes.
	const FVector Gap(FMath::Max(0., P.X - Size.X), FMath::Max(0., P.Y - Size.Y),
	                  FMath::Max(0., P.Z - Size.Z - (HalfHeight - Radius)));
	return Gap.SizeSquared() <= FMath::Square(Radius);
}

AExtractionLadder::AExtractionLadder()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	bReplicates = true;
	bAlwaysRelevant = true;
}

void AExtractionLadder::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const
{
	Super::GetLifetimeReplicatedProps(Out);
	auto& OutLifetimeProps = Out;
	DOREPLIFETIME(AExtractionLadder, Occupant);
	DOREPLIFETIME(AExtractionLadder, SourceId);
	DOREPLIFETIME(AExtractionLadder, Bottom);
	DOREPLIFETIME(AExtractionLadder, Top);
	DOREPLIFETIME(AExtractionLadder, BottomExit);
	DOREPLIFETIME(AExtractionLadder, TopExit);
	DOREPLIFETIME(AExtractionLadder, BottomEntry);
	DOREPLIFETIME(AExtractionLadder, TopEntry);
	DOREPLIFETIME(AExtractionLadder, TopClearance);
	DOREPLIFETIME(AExtractionLadder, GripOrigin);
	DOREPLIFETIME(AExtractionLadder, RungSpacing);
	DOREPLIFETIME(AExtractionLadder, GripHalfWidth);
	DOREPLIFETIME(AExtractionLadder, Rungs);
}

bool AExtractionLadder::Claim(ACharacter* Character)
{
	if (!HasAuthority() || !IsValid(Character) || (IsValid(Occupant) && Occupant != Character)) return false;
	Occupant = Character;
	ForceNetUpdate();
	return true;
}

void AExtractionLadder::Release(ACharacter* Character)
{
	if (HasAuthority() && Occupant == Character)
	{
		Occupant = nullptr;
		ForceNetUpdate();
	}
}
