#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "AnimNode_RifleLocomotion.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct BROKENEDGE_API FAnimNode_RifleLocomotion : public FAnimNode_Base
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Links)
	FPoseLink Source;
	UPROPERTY()
	FAnimNode_BlendSpacePlayer_Standalone Player;
	UPROPERTY()
	FAnimNode_BlendSpacePlayer_Standalone AimPlayer;
	bool bEnabled = false;
	bool bAim = false;
	float Weight = 0.f;
	TArray<FTransform> LastPose;
	TArray<FTransform> BlendFromPose;
	bool bStartBlend = false;
	float PoseBlend = 1.f;
	float FrameDelta = 0.f;
	virtual bool HasPreUpdate() const override { return true; }
	virtual void PreUpdate(const UAnimInstance* Instance) override;
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
};
