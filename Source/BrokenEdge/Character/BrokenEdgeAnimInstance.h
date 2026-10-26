// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "BrokenEdgeAnimInstance.generated.h"

enum class ETurningInPlace : uint8;
/**动画状态机流程：
* Standing Idle->Equipped->Slot "WeaponSlot"->WeaponSlot->
AimOffsets->FullBody->TransformRightHand->TransformLeftHand->
TransformHand+Unequipped(Blend Poses by bool)->Output Pose

 */
UCLASS()
class BROKENEDGE_API UBrokenEdgeAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Movement|Validation")
	bool bRifleLocomotionEnabled = false;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Movement|Validation")
	FString RifleLocomotionAsset;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Movement|Validation")
	FVector RifleLocomotionCoordinates = FVector::ZeroVector;
	/** 初始化动画实例 */
	virtual void NativeInitializeAnimation() override;

	/**
	 * 更新动画实例
	 * @param DeltaTime 帧时间间隔
	*/
	virtual void NativeUpdateAnimation(float DeltaTime) override;

	/** Opt in only the calibrated third-person animation blueprint and weapon mesh. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon Grip")
	TObjectPtr<class USkeletalMesh> GripReachCorrectionWeaponMesh;

private:
	UPROPERTY(BlueprintReadOnly, Category = "Weapon Grip", meta = (AllowPrivateAccess = "true"))
	bool bUseGripReachCorrection = false;

	TSet<TWeakObjectPtr<class USkeletalMesh>> ReportedInvalidGripMeshes;

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

	/** 当前装备的武器 */
	class AWeapon* EquippedWeapon;

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
	bool bDefeated;

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

	/** 瞄准偏航 */
	UPROPERTY(BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	float AO_Yaw;

	/** 瞄准俯仰 */
	UPROPERTY(BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	float AO_Pitch;

	/** 左手变换 */
	UPROPERTY(BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	FTransform LeftHandTransform;

	/** 是否为本地控制 */
	// UPROPERTY(BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	// bool bLocallyController;

	/** 右手旋转 */
	UPROPERTY(BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	FRotator RightHandRotation;

	/** 是否使用FABRIK */
	UPROPERTY(BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	bool bUseFABRIK;

	/** 是否使用瞄准偏移 */
	UPROPERTY(BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	bool bUseAimOffsets;

	/** 是否变换右手 */
	UPROPERTY(BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	bool bTransformRightHand;
};
