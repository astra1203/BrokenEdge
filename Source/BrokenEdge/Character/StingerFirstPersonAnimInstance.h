#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "StingerFirstPersonAnimInstance.generated.h"

/** 仅本地显示的第一人称动画，不执行换弹或伤害结算。 */
UCLASS(Transient, Blueprintable)
class BROKENEDGE_API UStingerFirstPersonAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stinger")
	TObjectPtr<UAnimSequence> IdleAnimation;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stinger")
	TObjectPtr<UAnimSequence> AimAnimation;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stinger")
	TObjectPtr<UAnimSequence> SprintAnimation;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stinger")
	TObjectPtr<UAnimSequence> JumpAnimation;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ActiveAnimation;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Stinger|Validation")
	FName CurrentVisualState;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Stinger|Validation")
	FString CurrentAnimationName;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Stinger|Validation")
	bool bMissingStateAnimation = false;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Stinger|Validation")
	bool bLocomotionMotion = false;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Stinger|Validation")
	bool bLoopActiveAnimation = true;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Stinger|Validation")
	float ActivePlayRate = 1.f;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LocomotionReference;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LocomotionTransition;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	FName LocomotionTarget;
	float TransitionRemaining = 0.f;
	/** 起跳/落地一次性动作，播完自动回到循环姿势。 */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> AirTransition;
	FName AirTransitionState;
	float AirTransitionRemaining = 0.f;
	/** 开镜/关镜一次性过渡。 */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> AimTransition;
	FName AimTransitionState;
	float AimTransitionRemaining = 0.f;
	bool bWasFalling = false;
	bool bWasAiming = false;
	TWeakObjectPtr<class AWeapon> LocomotionWeapon;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};

/** Visual magazine only. No ammo or equipment settlement notifies. */
UCLASS(Transient)
class BROKENEDGE_API UStingerFirstPersonWeaponAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};
