#include "AnimNode_RifleLocomotion.h"
#include "RifleLocomotionSet.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Character/BrokenEdgeAnimInstance.h"
#include "Animation/BlendSpace.h"
#include "BrokenEdge/Extraction/ExtractionMeleeComponent.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "BrokenEdge/Weapon/WeaponPresentation.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AnimationRuntime.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimInstanceProxy.h"

void FAnimNode_RifleLocomotion::PreUpdate(const UAnimInstance* Instance)
{
	auto* Character = Cast<ABrokenEdgeCharacter>(Instance->TryGetPawnOwner());
	const auto* Weapon = Character ? Character->GetEquippedWeapon() : nullptr;
	const auto* Set = Weapon && Weapon->Presentation ? Weapon->Presentation->Locomotion.Get() : nullptr;
	bEnabled = Set && Set->Standing && Set->Crouching && Character && !Character->Melee->Active
		&& !Character->IsTraversing() && !Character->IsDefeated() && !Character->GetCharacterMovement()->IsFalling();
	auto* State = Cast<UBrokenEdgeAnimInstance>(const_cast<UAnimInstance*>(Instance));
	if (State)
	{
		State->bRifleLocomotionEnabled = bEnabled;
		State->RifleLocomotionAsset.Reset();
	}
	if (!bEnabled) return;
	UBlendSpace* Desired = Character->bIsCrouched ? Set->Crouching : Set->Standing;
	bStartBlend |= Player.GetBlendSpace() != Desired;
	Player.SetBlendSpace(Desired);
	const FVector Velocity = Character->GetVelocity();
	const float Speed = Velocity.Size2D();
	// 冲刺由状态驱动并带保持时间：撞墙、上台阶、网络修正让速度瞬间掉到 0 时，
	// 不能让姿势跟着在冲刺与跑之间来回跳；真正的停步会先让 IsSprinting 失效再平滑退回。
	const float DeltaSeconds = FMath::Max(FrameDelta, 0.f);
	if (Character->IsSprinting() && !Character->bIsCrouched)
		SprintPoseHold = Set->SprintPoseHoldSeconds;
	else SprintPoseHold = FMath::Max(0.f, SprintPoseHold - DeltaSeconds);
	const bool bSprint = SprintPoseHold > 0.f;
	const float NaturalSpeed = Character->bIsCrouched ? Set->CrouchSpeed : bSprint ? Set->SprintSpeed : Set->RunSpeed;
	float Direction = Speed > 5.f
		                  ? FRotator::NormalizeAxis(Velocity.Rotation().Yaw - Character->GetActorRotation().Yaw)
		                  : 0.f;
	float Coordinate;
	if (bSprint)
	{
		// 冲刺采样只存在于 ±SprintDirectionLimit，超出范围钳制，避免角度在边界附近抖动。
		Direction = FMath::Clamp(Direction, -Set->SprintDirectionLimit, Set->SprintDirectionLimit);
		Coordinate = 850.f;
	}
	else Coordinate = FMath::Min(Speed, NaturalSpeed);
	SmoothedCoordinate = Set->CoordinateInterpSpeed > 0.f && DeltaSeconds > 0.f
		                     ? FMath::FInterpTo(SmoothedCoordinate, Coordinate, DeltaSeconds, Set->CoordinateInterpSpeed)
		                     : Coordinate;
	Player.SetPosition(FVector(Direction, SmoothedCoordinate, 0));
	Player.SetPlayRate(Speed > NaturalSpeed ? FMath::Clamp(Speed / NaturalSpeed, .5f, 3.f) : 1.f);
	if (State)
	{
		State->RifleLocomotionAsset = Player.GetBlendSpace()->GetPathName();
		State->RifleLocomotionCoordinates = Player.GetPosition();
	}
	UBlendSpace* Aim = Character->bIsCrouched ? Set->CrouchAimOffset : Set->AimOffset;
	bAim = Aim != nullptr;
	AimPlayer.SetBlendSpace(Aim);
	AimPlayer.SetPosition(
		FVector(FMath::Clamp(FRotator::NormalizeAxis(Character->GetBaseAimRotation().Pitch), -80.f, 80.f), 0, 0));
	// AO_Rifle 是 1D（仅俯仰），左右瞄准由脊椎偏航补齐，避免瞄准方向与身体朝向脱节。
	AimYaw = bAim
		         ? FMath::Clamp(
			         FRotator::NormalizeAxis(
				         Character->GetBaseAimRotation().Yaw - Character->GetActorRotation().Yaw),
			         -Set->AimYawClamp, Set->AimYawClamp) * Set->AimYawScale
		         : 0.f;
}

void FAnimNode_RifleLocomotion::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	Source.Initialize(Context);
	Player.Initialize_AnyThread(Context);
	AimPlayer.Initialize_AnyThread(Context);
	Weight = 0.f;
	LastPose.Reset();
	BlendFromPose.Reset();
	PoseBlend = 1.f;
	SprintPoseHold = 0.f;
	SmoothedCoordinate = 0.f;
	SmoothedAimYaw = 0.f;
	AimYaw = 0.f;
}

void FAnimNode_RifleLocomotion::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Source.CacheBones(Context);
	Player.CacheBones_AnyThread(Context);
	AimPlayer.CacheBones_AnyThread(Context);
}

void FAnimNode_RifleLocomotion::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	Source.Update(Context);
	FrameDelta = Context.GetDeltaTime();
	SmoothedAimYaw = FMath::FInterpTo(SmoothedAimYaw, AimYaw, Context.GetDeltaTime(), 12.f);
	Weight = FMath::FInterpConstantTo(Weight, bEnabled ? 1.f : 0.f, Context.GetDeltaTime(), 1.f / .15f);
	if (Weight > 0.f)
	{
		Player.Update_AnyThread(Context);
		if (bAim) AimPlayer.Update_AnyThread(Context);
	}
}

void FAnimNode_RifleLocomotion::Evaluate_AnyThread(FPoseContext& Output)
{
	Source.Evaluate(Output);
	if (Weight <= 0.f) return;
	FPoseContext Movement(Output);
	Player.Evaluate_AnyThread(Movement);
	if (bAim)
	{
		FPoseContext Aim(Output);
		AimPlayer.Evaluate_AnyThread(Aim);
		FAnimationPoseData BaseData(Movement), AimData(Aim);
		FAnimationRuntime::AccumulateAdditivePose(BaseData, AimData, 1.f, AAT_RotationOffsetMeshSpace);
		Movement.Pose.NormalizeRotations();
	}
	// 左右瞄准偏移：把偏航平均摊到脊椎三节，上半身转向瞄准方向，双腿保持原朝向。
	if (bAim && FMath::Abs(SmoothedAimYaw) > .5f)
	{
		const FBoneContainer& Bones = Output.AnimInstanceProxy->GetRequiredBones();
		static const FName SpineBones[] = {TEXT("Spine"), TEXT("Spine1"), TEXT("Spine2")};
		constexpr int32 SpineCount = UE_ARRAY_COUNT(SpineBones);
		const FQuat Step(FVector::UpVector, FMath::DegreesToRadians(SmoothedAimYaw / SpineCount));
		FCSPose<FCompactPose> SpinePose;
		SpinePose.InitPose(Movement.Pose);
		TArray<FBoneTransform, TInlineAllocator<SpineCount>> Changes;
		for (int32 Index = 0; Index < SpineCount; ++Index)
		{
			FBoneReference Reference;
			Reference.BoneName = SpineBones[Index];
			Reference.Initialize(Bones);
			if (!Reference.IsValidToEvaluate(Bones)) continue;
			const FCompactPoseBoneIndex BoneIndex = Reference.GetCompactPoseIndex(Bones);
			FTransform Transform = SpinePose.GetComponentSpaceTransform(BoneIndex);
			Transform.SetRotation(Step * Transform.GetRotation());
			Changes.Emplace(BoneIndex, Transform);
		}
		if (Changes.Num() > 0)
		{
			SpinePose.LocalBlendCSBoneTransforms(Changes, 1.f);
			FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(SpinePose), Movement.Pose);
		}
	}
	if (bStartBlend)
	{
		BlendFromPose = LastPose;
		PoseBlend = BlendFromPose.Num() == Movement.Pose.GetNumBones() ? 0.f : 1.f;
		bStartBlend = false;
	}
	PoseBlend = FMath::Min(1.f, PoseBlend + FrameDelta / .15f);
	LastPose.SetNum(Movement.Pose.GetNumBones());
	for (int32 Index = 0; Index < Output.Pose.GetNumBones(); ++Index)
	{
		const FCompactPoseBoneIndex Bone(Index);
		if (PoseBlend < 1.f) Movement.Pose[Bone].Blend(BlendFromPose[Index], Movement.Pose[Bone], PoseBlend);
		LastPose[Index] = Movement.Pose[Bone];
		Output.Pose[Bone].Blend(Output.Pose[Bone], Movement.Pose[Bone], Weight);
	}
}
