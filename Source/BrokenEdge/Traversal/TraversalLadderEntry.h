#pragma once

#include "CoreMinimal.h"
#include "TraversalLadderEntry.generated.h"

/** Original upright entry box, relative to its ladder. Zero extent disables an unbound entry. */
USTRUCT(BlueprintType)
struct BROKENEDGE_API FTraversalLadderEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	FTransform Transform = FTransform::Identity;
	UPROPERTY(EditAnywhere)
	FVector Extent = FVector::ZeroVector;

	bool OverlapsCapsule(const FTransform& LadderTransform, const FVector& Center, float Radius, float HalfHeight) const;
};
