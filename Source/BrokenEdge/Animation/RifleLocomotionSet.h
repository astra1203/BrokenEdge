#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RifleLocomotionSet.generated.h"

class UAnimSequence;
class UBlendSpace;

/** Opt-in presentation only; movement and combat remain authoritative elsewhere. */
UCLASS(BlueprintType)
class BROKENEDGE_API URifleLocomotionSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1"))
	float CrouchSpeed = 128.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1"))
	float SprintSpeed = 561.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UBlendSpace> Standing;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UBlendSpace> Crouching;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UBlendSpace> AimOffset;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UBlendSpace> CrouchAimOffset;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> ViewIdle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> ViewWalk;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> ViewRun;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> ViewSprint;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> WalkToRun;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> RunToWalk;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> WalkToSprint;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> RunToSprint;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> SprintToWalk;
	/** 第一人称开镜过渡；为空时沿用 0.15 秒姿势混合。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View|First Person")
	TObjectPtr<UAnimSequence> ViewAimOn;
	/** 第一人称关镜过渡；为空时沿用 0.15 秒姿势混合。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View|First Person")
	TObjectPtr<UAnimSequence> ViewAimOff;
	/** 第一人称起跳；为空时回退到 ViewJump 或 Idle。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View|First Person")
	TObjectPtr<UAnimSequence> ViewJumpStart;
	/** 第一人称空中循环；为空时回退到 ViewJump 或 Idle。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View|First Person")
	TObjectPtr<UAnimSequence> ViewJumpLoop;
	/** 第一人称落地；为空时直接回到循环姿势。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View|First Person")
	TObjectPtr<UAnimSequence> ViewJumpLand;
	/**
	 * 第三人称跳跃（官方 AR_M_3P_*Jump*）：原地起跳/空中/落地与跑动起跳三段，FallLoop 用于未起跳直接坠落。
	 * JumpLoop 为空时保持旧行为：空中回退到原有动画路径。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Third Person|Jump")
	TObjectPtr<UAnimSequence> JumpStart;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Third Person|Jump")
	TObjectPtr<UAnimSequence> JumpLoop;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Third Person|Jump")
	TObjectPtr<UAnimSequence> JumpLand;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Third Person|Jump")
	TObjectPtr<UAnimSequence> RunJumpStart;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Third Person|Jump")
	TObjectPtr<UAnimSequence> RunJumpLoop;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Third Person|Jump")
	TObjectPtr<UAnimSequence> RunJumpLand;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Third Person|Jump")
	TObjectPtr<UAnimSequence> FallLoop;
	/** 落地动作占满权重的比例，之后在剩余时间里淡回移动混合。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1"), Category="Third Person|Jump")
	float LandHoldFraction = .45f;
	/** 第一人称播放速率匹配上限，避免极高速时动画抽搐。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1"), Category="View|First Person")
	float ViewPlayRateMax = 2.5f;
	/** 第三人称瞄准左右偏移：把 (瞄准朝向 - 身体朝向) 的偏航按此比例分配到脊椎上。0 关闭。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1"), Category="Third Person")
	float AimYawScale = .6f;
	/** 瞄准偏航的最大参与角度，超过部分留给身体转向。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", Units="deg"), Category="Third Person")
	float AimYawClamp = 70.f;
	/**
	 * 冲刺姿势保持时长：IsSprinting 成立期间锁定冲刺姿势，掉线后仍保留这么久。
	 * 避免瞬时速度跌破阈值（撞墙、台阶、网络修正）时姿势在冲刺与跑之间反复跳。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", Units="s"), Category="Third Person")
	float SprintPoseHoldSeconds = .18f;
	/** 速度轴坐标的平滑速度，让冲刺↔跑切换是过渡而不是硬切。0 表示不平滑。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"), Category="Third Person")
	float CoordinateInterpSpeed = 12.f;
	/** 冲刺采样只覆盖 ±这个角度，超出部分钳制到边界，避免 45° 附近来回跳。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1", Units="deg"), Category="Third Person")
	float SprintDirectionLimit = 45.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1"))
	float WalkSpeed = 150.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1"))
	float RunSpeed = 600.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1"))
	float RunThreshold = 375.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	float SpeedHysteresis = 20.f;
};
