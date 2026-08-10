// Fill out your copyright notice in the Description page of Project Settings.


#include "BrokenEdgeAnimInstance.h"

#include "BrokenEdgeCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"


void UBrokenEdgeAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	BrokenEdgeCharacter = Cast<ABrokenEdgeCharacter>(TryGetPawnOwner());
}

void UBrokenEdgeAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	
	// ========== 1. 获取角色引用 ==========
	// 延迟初始化角色指针，确保动画实例持有有效的角色引用
	if (BrokenEdgeCharacter == nullptr)
	{
		BrokenEdgeCharacter = Cast<ABrokenEdgeCharacter>(TryGetPawnOwner());
	}
	// 若无有效角色，直接返回
	if (BrokenEdgeCharacter == nullptr) return;
	
	// ========== 2. 收集基础运动状态 ==========
	// 计算地面移动速度（忽略Z轴，即垂直速度）
	FVector Velocity = BrokenEdgeCharacter->GetVelocity();
	Velocity.Z = 0.f;
	Speed = Velocity.Size();
	
	// 收集角色运动状态标志
	bIsInAir = BrokenEdgeCharacter->GetCharacterMovement()->IsFalling();           // 是否在空中
	bIsAccelerating = BrokenEdgeCharacter->GetCharacterMovement()->GetCurrentAcceleration().Size() > 0.f; // 是否在加速
	bWeaponEquipped = BrokenEdgeCharacter->IsWeaponEquipped();                     // 是否装备武器
	//@ TODO
	// EquippedWeapon = BrokenEdgeCharacter->GetEquippedWeapon();                     // 当前装备的武器
	bIsCrouched = BrokenEdgeCharacter->bIsCrouched;                               // 是否蹲伏
	bAiming = BrokenEdgeCharacter->IsAiming();                                     // 是否瞄准中
	//@ TODO
	// TurningInPlace = BrokenEdgeCharacter->GetTurningInPlace();                     // 原地转向状态
	bRotateRootBone = BrokenEdgeCharacter->ShouldRotateRootBone();                 // 是否旋转根骨骼
	bElimmed = BrokenEdgeCharacter->IsElimed();                                   // 是否已被淘汰
	// bHoldingTheFlag = BrokenEdgeCharacter->IsHoldingTheFlag();                     // 是否持有旗帜

	
	
	
	
	
	
	
}
