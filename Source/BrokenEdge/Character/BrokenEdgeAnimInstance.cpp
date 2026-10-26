// Fill out your copyright notice in the Description page of Project Settings.


#include "BrokenEdgeAnimInstance.h"

#include "BrokenEdgeCharacter.h"
#include "BrokenEdge/Extraction/ExtractionMeleeComponent.h"
#include "BrokenEdge/BrokenEdgeTypes/CombatState.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "BrokenEdge/Weapon/WeaponPresentation.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"


void UBrokenEdgeAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	BrokenEdgeCharacter = Cast<ABrokenEdgeCharacter>(TryGetPawnOwner());
}

void UBrokenEdgeAnimInstance::NativeUpdateAnimation(float DeltaTime)
{
	Super::NativeUpdateAnimation(DeltaTime);
	// Do not retain an old weapon's target when ownership, equipment or control changes.
	LeftHandTransform = FTransform::Identity;
	bUseFABRIK = false;
	bUseAimOffsets = false;
	bTransformRightHand = false;
	bUseGripReachCorrection = false;

	// ========== 1. 获取角色引用 ==========
	// 延迟初始化角色指针，确保动画实例持有有效的角色引用
	if (!IsValid(BrokenEdgeCharacter))
	{
		BrokenEdgeCharacter = Cast<ABrokenEdgeCharacter>(TryGetPawnOwner());
	}
	// 若无有效角色，直接返回
	if (!IsValid(BrokenEdgeCharacter)) return;

	// ========== 2. 收集基础运动状态 ==========
	// 计算地面移动速度（忽略Z轴，即垂直速度）
	FVector Velocity = BrokenEdgeCharacter->GetVelocity();
	Velocity.Z = 0.f;
	Speed = Velocity.Size();

	// 收集角色运动状态标志
	bIsInAir = BrokenEdgeCharacter->GetCharacterMovement()->IsFalling(); // 是否在空中
	bIsAccelerating = BrokenEdgeCharacter->GetCharacterMovement()->GetCurrentAcceleration().Size() > 0.f; // 是否在加速

	bWeaponEquipped = BrokenEdgeCharacter->IsWeaponEquipped() && !BrokenEdgeCharacter->Melee->Active;
	EquippedWeapon = BrokenEdgeCharacter->Melee->Active ? nullptr : BrokenEdgeCharacter->GetEquippedWeapon();

	bIsCrouched = BrokenEdgeCharacter->bIsCrouched; // 是否蹲伏
	bAiming = BrokenEdgeCharacter->IsAiming(); // 是否瞄准中

	TurningInPlace = BrokenEdgeCharacter->GetTurningInPlace(); // 原地转向状态
	bRotateRootBone = BrokenEdgeCharacter->ShouldRotateRootBone(); // 是否旋转根骨骼
	bDefeated = BrokenEdgeCharacter->IsDefeated(); // 是否已被淘汰
	//@ TODO
	// bHoldingTheFlag = BrokenEdgeCharacter->IsHoldingTheFlag();                     // 是否持有旗帜

	// ========== 3. 计算偏航角偏移（Yaw Offset） ==========
	// 用于实现移动方向与瞄准方向不一致时的身体转向动画
	FRotator AimRotation = BrokenEdgeCharacter->GetBaseAimRotation(); // 获取瞄准旋转
	FRotator MovementRotation = UKismetMathLibrary::MakeRotFromX(BrokenEdgeCharacter->GetVelocity()); // 获取移动方向旋转
	FRotator DeltaRot = UKismetMathLibrary::NormalizedDeltaRotator(MovementRotation, AimRotation); // 计算两者差异
	DeltaRotation = FMath::RInterpTo(DeltaRotation, DeltaRot, DeltaTime, 6.f); // 平滑插值
	YawOffset = DeltaRotation.Yaw; // 提取Yaw分量

	// ========== 4. 计算角色倾斜（Lean） ==========
	// 实现角色转向时的身体倾斜效果，增加动画自然感
	CharacterRotationLastFrame = CharacterRotation; // 保存上一帧旋转
	CharacterRotation = BrokenEdgeCharacter->GetActorRotation(); // 获取当前旋转
	const FRotator Delta =
		UKismetMathLibrary::NormalizedDeltaRotator(CharacterRotation, CharacterRotationLastFrame); // 计算帧间旋转差
	const float Target = Delta.Yaw / DeltaTime; // 计算旋转速度
	const float Interp = FMath::FInterpTo(Lean, Target, DeltaTime, 6.f); // 平滑插值
	Lean = FMath::Clamp(Interp, -90.f, 90.f); // 限制范围

	// ========== 5. 获取瞄准偏移（Aim Offset） ==========
	// 用于驱动动画蓝图中的瞄准偏移节点，实现上半身跟随瞄准
	AO_Yaw = BrokenEdgeCharacter->GetAO_Yaw(); // 水平瞄准偏移
	AO_Pitch = BrokenEdgeCharacter->GetAO_Pitch(); // 垂直瞄准偏移

	// ========== 6. 计算手部IK（Inverse Kinematics） ==========
	if (bWeaponEquipped && IsValid(EquippedWeapon) && EquippedWeapon->GetWeaponMesh() && BrokenEdgeCharacter->GetMesh()
		&& EquippedWeapon->GetOwner() == BrokenEdgeCharacter
		&& EquippedWeapon->GetWeaponState() == EWeaponState::EWS_Equipped
		&& !bDefeated && !BrokenEdgeCharacter->GetDisableGameplay() && !BrokenEdgeCharacter->IsTraversing())
	{
		const auto WeaponMesh = EquippedWeapon->GetWeaponMesh();
		const auto Body = BrokenEdgeCharacter->GetMesh();
		const bool bHasRightHandBone = Body->GetBoneIndex(BrokenEdgeCharacter->RightHandBone) != INDEX_NONE;
		const bool bHasLeftGrip = WeaponMesh->DoesSocketExist(TEXT("LeftHandSocket"));
		const bool bHasRightGrip = WeaponMesh->DoesSocketExist(TEXT("Hand_R"));
		if ((!bHasLeftGrip || !bHasRightGrip || !bHasRightHandBone)
			&& !ReportedInvalidGripMeshes.Contains(WeaponMesh->GetSkeletalMeshAsset()))
		{
			ReportedInvalidGripMeshes.Add(WeaponMesh->GetSkeletalMeshAsset());
			UE_LOG(LogTemp, Warning,
			       TEXT(
				       "Weapon grip target unavailable: mesh=%s LeftHandSocket=%d Hand_R=%d character=%s right bone=%s valid=%d"
			       ),
			       *GetPathNameSafe(WeaponMesh->GetSkeletalMeshAsset()), bHasLeftGrip, bHasRightGrip,
			       *GetNameSafe(BrokenEdgeCharacter), *BrokenEdgeCharacter->RightHandBone.ToString(),
			       bHasRightHandBone);
		}
		// ===== 6.1 左手IK =====
		if (bHasLeftGrip && bHasRightHandBone)
		{
			const FTransform Grip = EquippedWeapon->EquipmentGrip(false) * WeaponMesh->GetComponentTransform();
			FVector OutPosition;
			FRotator OutRotation;
			Body->TransformToBoneSpace(BrokenEdgeCharacter->RightHandBone, Grip.GetLocation(), Grip.Rotator(),
			                           OutPosition, OutRotation);
			LeftHandTransform.SetLocation(OutPosition);
			LeftHandTransform.SetRotation(OutRotation.Quaternion());
		}

		// ===== 6.2 右手LookAt（仅本地玩家） =====
		// 让右手（持枪）始终指向准星瞄准点
		if (BrokenEdgeCharacter->IsLocallyControlled() && bHasRightGrip && bHasRightHandBone)
		{
			// bLocallyController = true;
			// 获取武器上右手Socket的世界坐标
			FTransform RightHandTransform = EquippedWeapon->GetWeaponMesh()->GetSocketTransform(
				FName("Hand_R"), ERelativeTransformSpace::RTS_World);
			// 计算看向目标的旋转
			FRotator LookAtRotation = UKismetMathLibrary::FindLookAtRotation(
				RightHandTransform.GetLocation(),
				RightHandTransform.GetLocation() + (RightHandTransform.GetLocation() - BrokenEdgeCharacter->
					GetHitTarget())
			);
			// 平滑插值到目标旋转
			RightHandRotation = FMath::RInterpTo(RightHandRotation, LookAtRotation, DeltaTime, 30.f);
		}


		// ========== 7. IK系统开关控制 ==========
		// 默认：空闲状态启用FABRIK
		bUseFABRIK = BrokenEdgeCharacter->GetCombatState() == ECombatState::ECS_Unoccuiped;

		// 本地玩家特殊处理：非投掷手雷且已完成换弹
		bool bFABRIKOverride = BrokenEdgeCharacter->IsLocallyControlled() &&
			BrokenEdgeCharacter->GetCombatState() != ECombatState::ECS_ThrowingGrenade &&
			BrokenEdgeCharacter->bFinishedSwapping;

		if (bFABRIKOverride)
		{
			// 本地玩家重新加载时禁用FABRIK
			bUseFABRIK = !BrokenEdgeCharacter->IsLocallyReloading();
		}
		bUseFABRIK = bUseFABRIK && bHasLeftGrip && bHasRightHandBone;
		bUseGripReachCorrection = bUseFABRIK && bHasRightGrip && GripReachCorrectionWeaponMesh
			&& WeaponMesh->GetSkeletalMeshAsset() == GripReachCorrectionWeaponMesh;

		// 瞄准偏移启用条件：空闲状态且游戏未禁用
		bUseAimOffsets = BrokenEdgeCharacter->GetCombatState() == ECombatState::ECS_Unoccuiped && !BrokenEdgeCharacter->
			GetDisableGameplay();
		// 右手变换启用条件：空闲状态且游戏未禁用
		bTransformRightHand = BrokenEdgeCharacter->GetCombatState() == ECombatState::ECS_Unoccuiped
			&& BrokenEdgeCharacter->IsLocallyControlled() && bHasRightGrip && bHasRightHandBone;
		if (bRifleLocomotionEnabled && EquippedWeapon->Presentation && EquippedWeapon->Presentation->Locomotion && !
			bIsInAir)
		{
			// The opt-in node already applies the matching original rifle aim poses.
			bUseAimOffsets = false;
		}
	}
}
