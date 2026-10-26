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
	const float Direction = Speed > 5.f
		                        ? FRotator::NormalizeAxis(Velocity.Rotation().Yaw - Character->GetActorRotation().Yaw)
		                        : 0.f;
	const bool bSprint = Character->IsSprinting() && !Character->bIsCrouched && Speed > 5.f;
	const float NaturalSpeed = Character->bIsCrouched ? Set->CrouchSpeed : bSprint ? Set->SprintSpeed : Set->RunSpeed;
	const float Coordinate = bSprint ? 850.f : FMath::Min(Speed, NaturalSpeed);
	Player.SetPosition(FVector(Direction, Coordinate, 0));
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
