#pragma once

#include "Animation/AnimInstance.h"
#include "BonePose.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Extraction/ExtractionMeleeComponent.h"

/** Snapshot on the game thread. Retain locomotion below the shoulders and attack montages. */
struct FExtractionMeleePose
{
	TMap<FName, FTransform> Transforms;
	bool bEnabled = false;
	void Cache(const UAnimInstance* Instance)
	{
		const auto* C = Cast<ABrokenEdgeCharacter>(Instance->TryGetPawnOwner());
		bEnabled = C && C->Melee->Active && !C->IsDefeated() && !C->IsTraversing()
			&& !Instance->IsAnyMontagePlaying();
		if (bEnabled) Transforms = C->Melee->BodyHoldPose;
	}
	void Apply(FCompactPose& Pose) const
	{
		if (!bEnabled) return;
		const auto& Bones = Pose.GetBoneContainer();
		for (const auto& Pair : Transforms)
		{
			const int32 SkeletonIndex = Bones.GetReferenceSkeleton().FindBoneIndex(Pair.Key);
			if (SkeletonIndex == INDEX_NONE) continue;
			const auto Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(SkeletonIndex));
			if (Index != INDEX_NONE) Pose[Index] = Pair.Value;
		}
	}
};
