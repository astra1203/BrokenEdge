#include "AnimNode_WeaponGripReach.h"
#include "TwoBoneIK.h"

void FAnimNode_WeaponGripReach::InitializeBoneReferences(const FBoneContainer& Bones)
{
	for (auto Bone : {&RightArm, &RightForeArm, &RightHand, &LeftArm, &LeftForeArm, &LeftHand})
		Bone->Initialize(Bones);
}

bool FAnimNode_WeaponGripReach::IsValidToEvaluate(const USkeleton*, const FBoneContainer& Bones)
{
	for (auto Bone : {&RightArm, &RightForeArm, &RightHand, &LeftArm, &LeftForeArm, &LeftHand})
		if (!Bone->IsValidToEvaluate(Bones)) return false;
	return Bones.BoneIsChildOf(RightHand.BoneIndex, RightForeArm.BoneIndex)
		&& Bones.BoneIsChildOf(RightForeArm.BoneIndex, RightArm.BoneIndex)
		&& Bones.BoneIsChildOf(LeftHand.BoneIndex, LeftForeArm.BoneIndex)
		&& Bones.BoneIsChildOf(LeftForeArm.BoneIndex, LeftArm.BoneIndex);
}

void FAnimNode_WeaponGripReach::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms)
{
	if (!bEnabled || LeftHandTarget.ContainsNaN()) return;
	const auto& Bones = Output.Pose.GetPose().GetBoneContainer();
	auto Pose = [&](const FBoneReference& Bone) {
		return Output.Pose.GetComponentSpaceTransform(Bone.GetCompactPoseIndex(Bones));
	};
	FTransform Upper = Pose(RightArm), Lower = Pose(RightForeArm), Hand = Pose(RightHand);
	const FVector LeftRoot = Pose(LeftArm).GetLocation();
	const FVector LeftElbow = Pose(LeftForeArm).GetLocation();
	const FVector LeftTip = Pose(LeftHand).GetLocation();
	// Leave enough bend for the left elbow rather than parking on the singular fully extended pose.
	const double LeftReach = (FVector::Distance(LeftRoot, LeftElbow) + FVector::Distance(LeftElbow, LeftTip)) * .92;
	const double RightReach = (FVector::Distance(Upper.GetLocation(), Lower.GetLocation())
		+ FVector::Distance(Lower.GetLocation(), Hand.GetLocation())) * .98;
	const FVector GripDelta = Hand.TransformVector(LeftHandTarget.GetLocation());
	FVector Target = Hand.GetLocation();
	if (FVector::Distance(Target + GripDelta, LeftRoot) <= LeftReach) return;
	// Project into both arms' reachable volumes, without stretching either arm or rotating the gun.
	for (int32 Iteration = 0; Iteration < 12; ++Iteration)
	{
		Target = LeftRoot - GripDelta + (Target + GripDelta - LeftRoot).GetClampedToMaxSize(LeftReach);
		Target = Upper.GetLocation() + (Target - Upper.GetLocation()).GetClampedToMaxSize(RightReach);
	}
	AnimationCore::SolveTwoBoneIK(Upper, Lower, Hand, Lower.GetLocation(), Target, false, 1., 1.);
	OutBoneTransforms.Emplace(RightArm.GetCompactPoseIndex(Bones), Upper);
	OutBoneTransforms.Emplace(RightForeArm.GetCompactPoseIndex(Bones), Lower);
	OutBoneTransforms.Emplace(RightHand.GetCompactPoseIndex(Bones), Hand);
}
