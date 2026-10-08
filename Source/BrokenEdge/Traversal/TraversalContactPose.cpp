#include "TraversalContactPose.h"
#include "ExtractionTraversalComponent.h"
#include "ExtractionLadder.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "TwoBoneIK.h"

void FTraversalContactPose::Cache(const UAnimInstance* Instance)
{
	bEnabled = false;
	const auto* C = Cast<ABrokenEdgeCharacter>(Instance->TryGetPawnOwner());
	if (!C || !C->IsTraversing()) return;
	const auto& S = C->GetTraversal()->VisualState;
	bLadder = S.Kind == EExtractionTraversal::Ladder;
	if (!bLadder && S.Kind != EExtractionTraversal::Vault && S.Kind != EExtractionTraversal::Mantle) return;
	MeshToWorld = Instance->GetSkelMeshComponent()->GetComponentTransform();
	bArmsOnly = Instance->GetSkelMeshComponent() == C->GetFirstPersonMesh();
	Facing = S.Facing;
	Origin = S.Contact;
	const float Phase = S.Elapsed / FMath::Max(.01f, S.Duration);
	Weight = FMath::Clamp(Phase / .15f, 0.f, 1.f) * (1.f - FMath::Clamp((Phase - .28f) / .3f, 0.f, 1.f));
	if (bLadder && IsValid(S.Ladder))
	{
		Origin = S.Ladder->Point(S.Ladder->GripOrigin);
		RungSpacing = FMath::Max(10.f, S.Ladder->RungSpacing);
		HalfWidth = S.Ladder->GripHalfWidth;
		Weight = 1;
		Rungs.Reset();
		for (const auto& Rung : S.Ladder->Rungs) Rungs.Add(S.Ladder->Point(Rung));
	}
	bEnabled = Weight > KINDA_SMALL_NUMBER;
}

void FTraversalContactPose::Apply(FCompactPose& LocalPose) const
{
	if (!bEnabled) return;
	// Source ladder poses keep the torso behind the wrists. Calibrate the body
	// toward the rung plane without moving the authoritative collision capsule.
	if (bLadder && !bArmsOnly)
		LocalPose[FCompactPoseBoneIndex(0)].AddToTranslation(MeshToWorld.InverseTransformVectorNoScale(Facing * 15.f));
	FCSPose<FCompactPose> Pose;
	Pose.InitPose(LocalPose);
	const auto& Bones = LocalPose.GetBoneContainer();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Facing);
	float ContactAdvance = 0;
	const auto Solve = [&](const TCHAR* UpperName, const TCHAR* LowerName, const TCHAR* TipName, bool bRight,
	                       bool bFoot, bool bMeasure = false)
	{
		FBoneReference UpperBone, LowerBone, TipBone;
		UpperBone.BoneName = UpperName;
		LowerBone.BoneName = LowerName;
		TipBone.BoneName = TipName;
		UpperBone.Initialize(Bones);
		LowerBone.Initialize(Bones);
		TipBone.Initialize(Bones);
		if (!UpperBone.IsValidToEvaluate(Bones) || !LowerBone.IsValidToEvaluate(Bones) || !TipBone.
			IsValidToEvaluate(Bones))
			return;
		const auto U = UpperBone.GetCompactPoseIndex(Bones), L = LowerBone.GetCompactPoseIndex(Bones), T = TipBone.
			           GetCompactPoseIndex(Bones);
		FTransform Upper = Pose.GetComponentSpaceTransform(U), Lower = Pose.GetComponentSpaceTransform(L), Tip = Pose.
			           GetComponentSpaceTransform(T);
		const FVector Lateral = Right * (bRight ? 1.f : -1.f) * HalfWidth * (bFoot ? .75f : 1.f);
		FVector Target = Origin + Lateral;
		if (bLadder)
		{
			const FVector WorldTip = MeshToWorld.TransformPosition(Tip.GetLocation());
			const float Rung = (WorldTip.Z - Origin.Z) / RungSpacing;
			const float Base = FMath::FloorToFloat(Rung);
			// Hold a rung through most of the cycle; ease between adjacent rungs without stretching.
			Target.Z += (Base + FMath::SmoothStep(.4f, .6f, Rung - Base)) * RungSpacing;
			if (!Rungs.IsEmpty())
			{
				FVector Contact = Rungs.Last();
				if (WorldTip.Z <= Rungs[0].Z) Contact = Rungs[0];
				else
					for (int32 I = 1; I < Rungs.Num(); ++I)
						if (WorldTip.Z <= Rungs[I].Z)
						{
							const float Fraction = (WorldTip.Z - Rungs[I - 1].Z) / FMath::Max(
								1., Rungs[I].Z - Rungs[I - 1].Z);
							Contact = FMath::Lerp(Rungs[I - 1], Rungs[I], FMath::SmoothStep(.4f, .6f, Fraction));
							break;
						}
				Target = Contact + Lateral;
			}
			if (bFoot) Target -= Facing * 6.f;
		}
		else Target.Z += 3.f;
		Target = MeshToWorld.InverseTransformPosition(Target);
		const float Reach = FVector::Distance(Upper.GetLocation(), Lower.GetLocation()) + FVector::Distance(
			Lower.GetLocation(), Tip.GetLocation());
		if (bMeasure)
		{
			// Lean the visual pose toward a sloped ladder until both wrists can reach
			// their rungs. Preserve limb lengths and leave a small elbow bend.
			const FVector Delta = MeshToWorld.TransformVectorNoScale(Target - Upper.GetLocation());
			const float Forward = FVector::DotProduct(Delta, Facing);
			const float SideSquared = FMath::Max(0., Delta.SizeSquared() - Forward * Forward);
			const float ReachSquared = FMath::Square(Reach * .94f);
			if (SideSquared < ReachSquared)
				ContactAdvance = FMath::Max(ContactAdvance, Forward - FMath::Sqrt(ReachSquared - SideSquared));
			return;
		}
		// Retain the source reaching pose until the moving body makes this contact reachable.
		if (FVector::Distance(Upper.GetLocation(), Target) > Reach * .98f) return;
		AnimationCore::SolveTwoBoneIK(Upper, Lower, Tip, Lower.GetLocation(), Target, false, 1., 1.);
		TArray<FBoneTransform> Changes;
		Changes.Emplace(U, Upper);
		Changes.Emplace(L, Lower);
		Changes.Emplace(T, Tip);
		Changes.Sort(FCompareBoneTransformIndex());
		Pose.LocalBlendCSBoneTransforms(Changes, Weight);
	};
	if (bLadder)
	{
		Solve(TEXT("RightArm"), TEXT("RightForeArm"), TEXT("RightHand"), true, false, true);
		Solve(TEXT("LeftArm"), TEXT("LeftForeArm"), TEXT("LeftHand"), false, false, true);
		if (ContactAdvance > 0)
		{
			LocalPose[FCompactPoseBoneIndex(0)].AddToTranslation(MeshToWorld.InverseTransformVectorNoScale(
				Facing * FMath::Min(ContactAdvance, 60.f)));
			Pose.InitPose(LocalPose);
		}
	}
	Solve(TEXT("RightArm"), TEXT("RightForeArm"), TEXT("RightHand"), true, false);
	Solve(TEXT("LeftArm"), TEXT("LeftForeArm"), TEXT("LeftHand"), false, false);
	if (bLadder && !bArmsOnly)
	{
		Solve(TEXT("RightUpLeg"), TEXT("RightLeg"), TEXT("RightFoot"), true, true);
		Solve(TEXT("LeftUpLeg"), TEXT("LeftLeg"), TEXT("LeftFoot"), false, true);
	}
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(Pose), LocalPose);
}
