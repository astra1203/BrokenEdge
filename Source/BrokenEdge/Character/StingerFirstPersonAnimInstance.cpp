#include "StingerFirstPersonAnimInstance.h"
#include "BrokenEdgeCharacter.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "Animation/AnimNodeSpaceConversions.h"
#include "BoneControllers/AnimNode_TwoBoneIK.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

// 原生动画图负责基础姿势和视觉蒙太奇；AnimBP 资产只配置动画引用。
struct FStingerFirstPersonProxy : FAnimInstanceProxy
{
	FAnimNode_SequencePlayer_Standalone Player;
	FAnimNode_Slot Slot;
	FAnimNode_ConvertLocalToComponentSpace ToComponent;
	FAnimNode_TwoBoneIK LeftHandIK;
	FAnimNode_ConvertComponentToLocalSpace ToLocal;
	TArray<FTransform> LastPose;
	TArray<FTransform> BlendFromPose;
	float BlendAlpha = 1.f;
	float FrameDelta = 0.f;
	bool bStartBlend = false;

	explicit FStingerFirstPersonProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
	{
	}

	virtual void Initialize(UAnimInstance* Instance) override
	{
		FAnimInstanceProxy::Initialize(Instance);
		Player.SetSequence(CastChecked<UStingerFirstPersonAnimInstance>(Instance)->IdleAnimation);
		Player.SetLoopAnimation(true);
		Slot.SlotName = TEXT("DefaultSlot");
		Slot.Source.SetLinkNode(&Player);
		ToComponent.LocalPose.SetLinkNode(&Slot);
		LeftHandIK.ComponentPose.SetLinkNode(&ToComponent);
		LeftHandIK.IKBone.BoneName = TEXT("LeftHand");
		LeftHandIK.EffectorLocationSpace = BCS_BoneSpace;
		LeftHandIK.EffectorTarget.BoneReference.BoneName = TEXT("RightHand");
		LeftHandIK.JointTargetLocationSpace = BCS_ComponentSpace;
		LeftHandIK.JointTargetLocation = FVector(35.f, -20.f, 130.f);
		LeftHandIK.bAllowStretching = false;
		ToLocal.ComponentPose.SetLinkNode(&LeftHandIK);
		ToLocal.Initialize_AnyThread(FAnimationInitializeContext(this));
	}

	virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
	{
		FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
		FrameDelta = DeltaSeconds;
		UAnimSequence* Desired = CastChecked<UStingerFirstPersonAnimInstance>(Instance)->ActiveAnimation;
		if (Desired && Desired != Player.GetSequence())
		{
			Player.SetSequence(Desired);
			Player.SetAccumulatedTime(0.f);
			bStartBlend = true;
		}
		const auto Character = Cast<ABrokenEdgeCharacter>(Instance->TryGetPawnOwner());
		LeftHandIK.Alpha = 0.f;
		if (Character && Character->GetFirstPersonWeaponMesh() && Character->GetFirstPersonMesh()
			&& Character->GetFirstPersonWeaponMesh()->DoesSocketExist(TEXT("LeftHandSocket"))
			&& Character->GetCombatState() == ECombatState::ECS_Unoccuiped)
		{
			FRotator Rotation;
			Character->GetFirstPersonMesh()->TransformToBoneSpace(TEXT("RightHand"),
			                                                      Character->GetFirstPersonWeaponMesh()->
			                                                                 GetSocketLocation(TEXT("LeftHandSocket")),
			                                                      FRotator::ZeroRotator, LeftHandIK.EffectorLocation,
			                                                      Rotation);
			LeftHandIK.Alpha = 1.f;
		}
	}

	virtual void UpdateAnimationNode(const FAnimationUpdateContext& Context) override
	{
		ToLocal.Update_AnyThread(Context);
	}

	virtual bool Evaluate(FPoseContext& Output) override
	{
		ToLocal.CacheBones_AnyThread(FAnimationCacheBonesContext(this));
		ToLocal.Evaluate_AnyThread(Output);
		const int32 BoneCount = Output.Pose.GetNumBones();
		if (bStartBlend)
		{
			BlendFromPose = LastPose;
			BlendAlpha = BlendFromPose.Num() == BoneCount ? 0.f : 1.f;
			bStartBlend = false;
		}
		BlendAlpha = FMath::Min(1.f, BlendAlpha + FrameDelta / 0.15f);
		LastPose.SetNum(BoneCount);
		for (int32 Index = 0; Index < BoneCount; ++Index)
		{
			FTransform& Transform = Output.Pose[FCompactPoseBoneIndex(Index)];
			if (BlendAlpha < 1.f) Transform.Blend(BlendFromPose[Index], Transform, BlendAlpha);
			LastPose[Index] = Transform;
		}
		return true;
	}
};

void UStingerFirstPersonAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	ABrokenEdgeCharacter* Character = Cast<ABrokenEdgeCharacter>(TryGetPawnOwner());
	ActiveAnimation = IdleAnimation;
	CurrentVisualState = TEXT("Idle");
	bMissingStateAnimation = false;
	if (!Character) return;
	if (!Character->GetEquippedWeapon()) CurrentVisualState = TEXT("Unarmed");
	else if (Character->GetCharacterMovement()->IsFalling())
	{
		CurrentVisualState = TEXT("Airborne");
		bMissingStateAnimation = !JumpAnimation;
		if (JumpAnimation) ActiveAnimation = JumpAnimation;
	}
	else if (Character->IsSprinting())
	{
		CurrentVisualState = TEXT("Sprint");
		bMissingStateAnimation = !SprintAnimation;
		if (SprintAnimation) ActiveAnimation = SprintAnimation;
	}
	else if (const_cast<ABrokenEdgeCharacter*>(Character)->IsAiming())
	{
		CurrentVisualState = TEXT("Aim");
		bMissingStateAnimation = !AimAnimation;
		if (AimAnimation) ActiveAnimation = AimAnimation;
	}
	if (Character->GetCombatState() == ECombatState::ECS_Reloading) CurrentVisualState = TEXT("Reload");
	else if (Character->GetCombatState() == ECombatState::ECS_SwappingWeapon) CurrentVisualState = TEXT("Equip");
	CurrentAnimationName = ActiveAnimation ? ActiveAnimation->GetPathName() : FString();
}

FAnimInstanceProxy* UStingerFirstPersonAnimInstance::CreateAnimInstanceProxy()
{
	return new FStingerFirstPersonProxy(this);
}

void UStingerFirstPersonAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy)
{
	delete static_cast<FStingerFirstPersonProxy*>(Proxy);
}
