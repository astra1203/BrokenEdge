#include "StingerFirstPersonAnimInstance.h"
#include "BrokenEdgeCharacter.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "Animation/AnimNodeSpaceConversions.h"
#include "BoneControllers/AnimNode_TwoBoneIK.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TwoBoneIK.h"
#include "BrokenEdge/Traversal/TraversalContactPose.h"

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
	bool bWeaponFit = false;
	bool bActionMotion = false;
	FTransform RightTarget, LeftTarget, RightReference, LeftReference;
	bool bLeftActionMotion = false;
	FTraversalContactPose TraversalContacts;

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
		TraversalContacts.Cache(Instance);
		FrameDelta = DeltaSeconds;
		UAnimSequence* Desired = CastChecked<UStingerFirstPersonAnimInstance>(Instance)->ActiveAnimation;
		if (Desired && Desired != Player.GetSequence())
		{
			Player.SetSequence(Desired);
			Player.SetAccumulatedTime(0.f);
			bStartBlend = true;
		}
		const auto Character = Cast<ABrokenEdgeCharacter>(Instance->TryGetPawnOwner());
		bWeaponFit = Character && Character->UsesFirstPersonWeaponFit();
		bActionMotion = Instance->GetCurrentActiveMontage() != nullptr;
		if (bWeaponFit)
		{
			const FTransform& Component = Character->GetFirstPersonMesh()->GetComponentTransform();
			RightTarget = Character->GetFirstPersonGripTarget(TEXT("Hand_R")).GetRelativeTransform(Component);
			LeftTarget = Character->GetFirstPersonGripTarget(TEXT("LeftHandSocket")).GetRelativeTransform(Component);
			RightReference = Character->FirstPersonWeaponFit.SourceRightHand;
			LeftReference = Character->FirstPersonWeaponFit.SourceLeftHand;
			bLeftActionMotion = bActionMotion;
			const float Reload = Character->GetFirstPersonReloadProgress();
			if (Reload >= 0.f)
			{
				const auto& Fit = Character->FirstPersonWeaponFit;
				const FTransform Gun = Character->GetFirstPersonGripTarget(TEXT("Hand_R"));
				const FTransform Grip = Character->GetFirstPersonWeaponMesh()->GetSocketTransform(
					TEXT("Hand_R"), RTS_Component);
				const FTransform Magazine = (Fit.MagazineGrip * Grip.Inverse() * Gun).GetRelativeTransform(Component);
				if (Reload < Fit.MagazineOut)
				{
					FTransform Goal;
					Goal.Blend(LeftTarget, Magazine, FMath::Clamp(Reload / Fit.MagazineOut, 0.f, 1.f));
					LeftTarget = Goal;
					bLeftActionMotion = false;
				}
				else if (Reload <= Fit.MagazineIn)
				{
					LeftTarget = Magazine;
					LeftReference.Blend(Fit.SourceLeftMagazineOut, Fit.SourceLeftMagazineIn,
					                    (Reload - Fit.MagazineOut) / (Fit.MagazineIn - Fit.MagazineOut));
				}
				else
				{
					FTransform Goal;
					Goal.Blend(Magazine, LeftTarget, FMath::Clamp((Reload - Fit.MagazineIn) / .15f, 0.f, 1.f));
					LeftTarget = Goal;
					bLeftActionMotion = false;
				}
			}
		}
		LeftHandIK.Alpha = 0.f;
		if (Character && !Character->IsTraversing() && Character->GetFirstPersonWeaponMesh() && Character->
			GetFirstPersonMesh()
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
		if (bWeaponFit) Slot.Evaluate_AnyThread(Output);
		else ToLocal.Evaluate_AnyThread(Output);
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
		if (bWeaponFit)
		{
			FCSPose<FCompactPose> Pose;
			Pose.InitPose(Output.Pose);
			auto SolveArm = [&](FName UpperName, FName LowerName, FName HandName, const FTransform& Goal,
			                    const FTransform& Reference, bool bRight)
			{
				FBoneReference UpperBone, LowerBone, HandBone;
				UpperBone.BoneName = UpperName;
				LowerBone.BoneName = LowerName;
				HandBone.BoneName = HandName;
				const FBoneContainer& Bones = GetRequiredBones();
				UpperBone.Initialize(Bones);
				LowerBone.Initialize(Bones);
				HandBone.Initialize(Bones);
				if (!UpperBone.IsValidToEvaluate(Bones) || !LowerBone.IsValidToEvaluate(Bones) || !HandBone.
					IsValidToEvaluate(Bones))
					return;
				const auto UpperIndex = UpperBone.GetCompactPoseIndex(Bones), LowerIndex = LowerBone.
					           GetCompactPoseIndex(Bones), HandIndex = HandBone.GetCompactPoseIndex(Bones);
				FTransform Upper = Pose.GetComponentSpaceTransform(UpperIndex), Lower = Pose.
					           GetComponentSpaceTransform(LowerIndex), Hand = Pose.
					           GetComponentSpaceTransform(HandIndex);
				const FTransform SourceHand = Hand;
				const bool bMotion = bRight ? bActionMotion : bLeftActionMotion;
				const FVector Motion = bMotion
					                       ? SourceHand.GetLocation() - Reference.GetLocation()
					                       : FVector::ZeroVector;
				// Protract the clavicle before solving the arm; keep bone lengths intact.
				FBoneReference Clavicle;
				Clavicle.BoneName = bRight ? TEXT("RightShoulder") : TEXT("LeftShoulder");
				Clavicle.Initialize(Bones);
				if (Clavicle.IsValidToEvaluate(Bones))
				{
					const auto Index = Clavicle.GetCompactPoseIndex(Bones);
					FTransform Shoulder = Pose.GetComponentSpaceTransform(Index);
					const FVector Original = Upper.GetLocation() - Shoulder.GetLocation();
					const float ArmLength = FVector::Distance(Upper.GetLocation(), Lower.GetLocation()) +
						FVector::Distance(Lower.GetLocation(), Hand.GetLocation());
					const FVector GoalDirection = (Goal.GetLocation() + Motion - Shoulder.GetLocation()).
						GetSafeNormal();
					FVector Direction = Original.GetSafeNormal();
					for (float Alpha = .05f; Alpha <= .65f && FVector::Distance(
						     Shoulder.GetLocation() + Direction * Original.Size(),
						     Goal.GetLocation() + Motion) > ArmLength * .9f; Alpha += .05f)
						Direction = FMath::Lerp(Original.GetSafeNormal(), GoalDirection, Alpha).GetSafeNormal();
					Shoulder.SetRotation(
						FQuat::FindBetweenNormals(Original.GetSafeNormal(), Direction) * Shoulder.GetRotation());
					TArray<FBoneTransform> Change;
					Change.Emplace(Index, Shoulder);
					Pose.LocalBlendCSBoneTransforms(Change, 1.f);
					Upper = Pose.GetComponentSpaceTransform(UpperIndex);
					Lower = Pose.GetComponentSpaceTransform(LowerIndex);
					Hand = Pose.GetComponentSpaceTransform(HandIndex);
				}
				const FVector Joint = Upper.GetLocation() + FVector(bRight ? -25.f : 25.f, -30.f, -35.f);
				AnimationCore::SolveTwoBoneIK(Upper, Lower, Hand, Joint, Goal.GetLocation() + Motion, false, 1., 1.);
				Hand.SetRotation(bMotion
					                 ? SourceHand.GetRotation() * Reference.GetRotation().Inverse() * Goal.GetRotation()
					                 : Goal.GetRotation());
				TArray<FBoneTransform, TInlineAllocator<3>> Changes;
				Changes.Emplace(UpperIndex, Upper);
				Changes.Emplace(LowerIndex, Lower);
				Changes.Emplace(HandIndex, Hand);
				Changes.Sort(FCompareBoneTransformIndex());
				Pose.LocalBlendCSBoneTransforms(Changes, 1.f);
			};
			SolveArm(TEXT("RightArm"),TEXT("RightForeArm"),TEXT("RightHand"), RightTarget, RightReference, true);
			FBoneReference RightHandBone;
			RightHandBone.BoneName = TEXT("RightHand");
			RightHandBone.Initialize(GetRequiredBones());
			const FTransform FinalRight = Pose.GetComponentSpaceTransform(
				RightHandBone.GetCompactPoseIndex(GetRequiredBones()));
			const FTransform MovingLeftTarget = LeftTarget.GetRelativeTransform(RightTarget) * FinalRight;
			SolveArm(TEXT("LeftArm"),TEXT("LeftForeArm"),TEXT("LeftHand"), MovingLeftTarget, LeftReference, false);
			FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(Pose), Output.Pose);
		}
		TraversalContacts.Apply(Output.Pose);
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

struct FStingerWeaponProxy : FAnimInstanceProxy
{
	TArray<FTransform> MeshReferencePose;
	FTransform Magazine;
	FName Bone;
	bool bMove = false;

	explicit FStingerWeaponProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
	{
	}

	virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
	{
		FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
		const auto View = CastChecked<UStingerFirstPersonWeaponAnimInstance>(Instance);
		if (const auto Mesh = View->GetSkelMeshComponent()->GetSkeletalMeshAsset())
			MeshReferencePose = Mesh->GetRefSkeleton().GetRefBonePose();
		bMove = false;
		const auto Character = Cast<ABrokenEdgeCharacter>(Instance->GetOwningActor());
		if (!Character || !Character->UsesFirstPersonWeaponFit()) return;
		const auto EquippedWeapon = Character->GetEquippedWeapon();
		// Replicated drop/death can clear the weapon before the visual mesh's next tick.
		if (!IsValid(EquippedWeapon) || !IsValid(EquippedWeapon->GetWeaponMesh())) return;
		// Preserve the gameplay weapon's evaluated rest pose (shared weapon skeletons
		// can have a different reference pose from this mesh's import bind pose).
		MeshReferencePose = EquippedWeapon->GetWeaponMesh()->GetBoneSpaceTransforms();
		const auto& Fit = Character->FirstPersonWeaponFit;
		Bone = Fit.MagazineBone;
		const float Progress = Character->GetFirstPersonReloadProgress();
		if (Progress < Fit.MagazineOut || Progress > Fit.MagazineIn) return;
		const FTransform Hand = Character->GetFirstPersonMesh()->GetSocketTransform(TEXT("LeftHand"));
		const FTransform Weapon = View->GetSkelMeshComponent()->GetComponentTransform();
		const FTransform Detached = (Fit.MagazineReference.GetRelativeTransform(Fit.MagazineGrip) * Hand).
			GetRelativeTransform(Weapon);
		const float Weight = FMath::Min(FMath::Clamp((Progress - Fit.MagazineOut) / .035f, 0.f, 1.f),
		                                FMath::Clamp((Fit.MagazineIn - Progress) / .05f, 0.f, 1.f));
		Magazine.Blend(Fit.MagazineReference, Detached, Weight);
		bMove = true;
	}

	virtual bool Evaluate(FPoseContext& Output) override
	{
		Output.ResetToRefPose();
		for (FCompactPoseBoneIndex Index : Output.Pose.ForEachBoneIndex())
		{
			const int32 MeshIndex = GetRequiredBones().MakeMeshPoseIndex(Index).GetInt();
			if (MeshReferencePose.IsValidIndex(MeshIndex)) Output.Pose[Index] = MeshReferencePose[MeshIndex];
		}
		if (!bMove) return true;
		FBoneReference Reference;
		Reference.BoneName = Bone;
		Reference.Initialize(GetRequiredBones());
		if (!Reference.IsValidToEvaluate(GetRequiredBones())) return true;
		FCSPose<FCompactPose> Pose;
		Pose.InitPose(Output.Pose);
		TArray<FBoneTransform> Changes;
		Changes.Emplace(Reference.GetCompactPoseIndex(GetRequiredBones()), Magazine);
		Pose.LocalBlendCSBoneTransforms(Changes, 1.f);
		FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(Pose), Output.Pose);
		return true;
	}
};

FAnimInstanceProxy* UStingerFirstPersonWeaponAnimInstance::CreateAnimInstanceProxy()
{
	return new FStingerWeaponProxy(this);
}

void UStingerFirstPersonWeaponAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy)
{
	delete static_cast<FStingerWeaponProxy*>(Proxy);
}
