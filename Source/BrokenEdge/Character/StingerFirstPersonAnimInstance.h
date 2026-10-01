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
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};
