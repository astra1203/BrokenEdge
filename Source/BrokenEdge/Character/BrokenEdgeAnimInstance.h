// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "BrokenEdgeAnimInstance.generated.h"

/**
 * 
 */
UCLASS()
class BROKENEDGE_API UBrokenEdgeAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** 初始化动画实例 */
	virtual void NativeInitializeAnimation() override;

	/**
	 * 更新动画实例
	 * @param DeltaTime 帧时间间隔
	*/
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	
private:
	/** 关联的玩家角色 */
	UPROPERTY(BlueprintReadOnly, Category = Character, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class ABrokenEdgeCharacter> BrokenEdgeCharacter;
	
	/** 移动速度 */
	UPROPERTY(BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	float Speed;
	
	/** 是否在空中 */
	UPROPERTY(BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	bool bIsInAir;

	/** 是否在加速 */
	UPROPERTY(BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	bool bIsAccelerating;
	
	/** 是否装备武器 */
	UPROPERTY(BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	bool bWeaponEquipped;
	
	/** 是否蹲下 */
	UPROPERTY(BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	bool bIsCrouched;
	
	/** 是否瞄准 */
	UPROPERTY(BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	bool bAiming;
	
	/** 是否旋转根骨骼 */
	UPROPERTY(BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	bool bRotateRootBone;
	
	/** 是否被淘汰 */
	UPROPERTY(BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	bool bElimmed;
};
