#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "AnimNode_WeaponGripReach.generated.h"

/** Move the supporting right arm only as far as needed to make a two-hand grip reachable.
 * Evaluates the current pose before left-hand FABRIK, preserving the animated gun rotation.
 */
USTRUCT(BlueprintInternalUseOnly)
struct BROKENEDGE_API FAnimNode_WeaponGripReach : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Grip, meta = (PinShownByDefault))
	FTransform LeftHandTarget = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Grip, meta = (PinShownByDefault))
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, Category = Bones)
	FBoneReference RightArm;
	UPROPERTY(EditAnywhere, Category = Bones)
	FBoneReference RightForeArm;
	UPROPERTY(EditAnywhere, Category = Bones)
	FBoneReference RightHand;
	UPROPERTY(EditAnywhere, Category = Bones)
	FBoneReference LeftArm;
	UPROPERTY(EditAnywhere, Category = Bones)
	FBoneReference LeftForeArm;
	UPROPERTY(EditAnywhere, Category = Bones)
	FBoneReference LeftHand;

	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
	                                               TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
};
