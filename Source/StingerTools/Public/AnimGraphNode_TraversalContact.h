#pragma once
#include "AnimGraphNode_Base.h"
#include "BrokenEdge/Animation/AnimNode_TraversalContact.h"
#include "AnimGraphNode_TraversalContact.generated.h"

UCLASS()
class STINGERTOOLS_API UAnimGraphNode_TraversalContact : public UAnimGraphNode_Base
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category=Settings)
	FAnimNode_TraversalContact Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type) const override
	{
		return NSLOCTEXT("Traversal", "Contacts", "Traversal Hand and Foot Contacts");
	}

	virtual FString GetNodeCategory() const override { return TEXT("Traversal"); }
};
