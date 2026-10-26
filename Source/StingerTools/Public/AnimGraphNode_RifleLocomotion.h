#pragma once
#include "AnimGraphNode_Base.h"
#include "BrokenEdge/Animation/AnimNode_RifleLocomotion.h"
#include "AnimGraphNode_RifleLocomotion.generated.h"

UCLASS()
class STINGERTOOLS_API UAnimGraphNode_RifleLocomotion : public UAnimGraphNode_Base
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category=Settings)
	FAnimNode_RifleLocomotion Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type) const override
	{
		return NSLOCTEXT("Stinger", "RifleLocomotion", "Original Rifle Locomotion (Opt In)");
	}

	virtual FString GetNodeCategory() const override { return TEXT("Stinger"); }
};
