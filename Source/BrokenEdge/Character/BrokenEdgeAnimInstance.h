// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "BrokenEdgeAnimInstance.generated.h"

enum class ETurningInPlace : uint8;
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
	virtual void NativeUpdateAnimation(float DeltaTime) override;
	
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
	
	/** 旋转变化量 */
	FRotator DeltaRotation;
	
	/** 偏航偏移 */
	UPROPERTY(BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	float YawOffset;
	
	/** 上一帧角色旋转 */
	FRotator CharacterRotationLastFrame;

	/** 当前角色旋转 */
	FRotator CharacterRotation;
	
	/** 原地转向状态 */
	UPROPERTY(BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	ETurningInPlace TurningInPlace;
	
	/** 倾斜角度 */
	UPROPERTY(BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	float Lean;
	
};
