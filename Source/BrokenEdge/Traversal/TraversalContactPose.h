#pragma once

#include "CoreMinimal.h"
#include "BonePose.h"

class UAnimInstance;

/** Game-thread snapshot, then a bounded limb-only correction on the animation worker. */
struct BROKENEDGE_API FTraversalContactPose
{
	void Cache(const UAnimInstance* Instance);
	void Apply(FCompactPose& Pose) const;
	bool bEnabled = false;

private:
	bool bLadder = false, bArmsOnly = false;
	FTransform MeshToWorld;
	FVector Origin = FVector::ZeroVector, Facing = FVector::ForwardVector;
	float Weight = 0, RungSpacing = 25, HalfWidth = 22;
	TArray<FVector> Rungs;
};
