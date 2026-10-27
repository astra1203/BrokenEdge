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
	/** 瞄准朝向与身体朝向的偏航差，已乘 AimYawScale 并钳制。 */
	float AimYaw = 0.f;
	/** 平滑后的偏航，供 Evaluate 使用，避免视角微抖直接传到脊椎。 */
	float SmoothedAimYaw = 0.f;
	/** 冲刺姿势保持计时：>0 时按冲刺处理，防止瞬时速度抖动切换姿势。 */
	float SprintPoseHold = 0.f;
	/** 平滑后的速度轴坐标。 */
	float SmoothedCoordinate = 0.f;
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
