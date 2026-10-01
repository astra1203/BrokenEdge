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
		UAnimSequence* Desired = CastChecked<UStingerFirstPersonAnimInstance>(Instance)->ActiveAnimation;
		if (Desired && Desired != Player.GetSequence())
		{
			Player.SetSequence(Desired);
			Player.SetAccumulatedTime(0.f);
		}
		const auto Character = Cast<ABrokenEdgeCharacter>(Instance->TryGetPawnOwner());
		LeftHandIK.Alpha = 0.f;
		if (Character && Character->GetFirstPersonWeaponMesh() && Character->GetFirstPersonMesh()
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
		return true;
	}
};

void UStingerFirstPersonAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	const ABrokenEdgeCharacter* Character = Cast<ABrokenEdgeCharacter>(TryGetPawnOwner());
	ActiveAnimation = IdleAnimation;
	if (!Character) return;
	if (Character->GetCharacterMovement()->IsFalling() && JumpAnimation) ActiveAnimation = JumpAnimation;
	else if (Character->IsSprinting() && SprintAnimation) ActiveAnimation = SprintAnimation;
	else if (const_cast<ABrokenEdgeCharacter*>(Character)->IsAiming() && AimAnimation) ActiveAnimation = AimAnimation;
}

FAnimInstanceProxy* UStingerFirstPersonAnimInstance::CreateAnimInstanceProxy()
{
	return new FStingerFirstPersonProxy(this);
}

void UStingerFirstPersonAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy)
{
	delete static_cast<FStingerFirstPersonProxy*>(Proxy);
}
