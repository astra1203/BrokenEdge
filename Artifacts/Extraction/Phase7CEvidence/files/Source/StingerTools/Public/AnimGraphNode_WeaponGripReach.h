#pragma once

#include "AnimGraphNode_SkeletalControlBase.h"
#include "BrokenEdge/Animation/AnimNode_WeaponGripReach.h"
#include "AnimGraphNode_WeaponGripReach.generated.h"

UCLASS()
class STINGERTOOLS_API UAnimGraphNode_WeaponGripReach : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_WeaponGripReach Node;
	virtual FText GetNodeTitle(ENodeTitleType::Type) const override { return GetControllerDescription(); }

protected:
	virtual FText GetControllerDescription() const override
	{
		return NSLOCTEXT("Stinger", "WeaponGripReach", "Keep Weapon Grip Within Reach");
	}

	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
};
