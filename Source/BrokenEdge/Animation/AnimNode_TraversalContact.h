#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "BrokenEdge/Traversal/TraversalContactPose.h"
#include "ExtractionMeleePose.h"
#include "AnimNode_TraversalContact.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct BROKENEDGE_API FAnimNode_TraversalContact : public FAnimNode_Base
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Links)
	FPoseLink Source;
	FTraversalContactPose Contacts;
	FExtractionMeleePose MeleePose;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override
	{
		Source.Initialize(Context);
	}

	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override
	{
		Source.CacheBones(Context);
	}

	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override { Source.Update(Context); }

	virtual void Evaluate_AnyThread(FPoseContext& Output) override
	{
		Source.Evaluate(Output);
		MeleePose.Apply(Output.Pose);
		Contacts.Apply(Output.Pose);
	}

	virtual bool HasPreUpdate() const override { return true; }

	virtual void PreUpdate(const UAnimInstance* Instance) override
	{
		Contacts.Cache(Instance);
		MeleePose.Cache(Instance);
	}
};
