#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpaceCenterInstanceGroup.generated.h"

class UHierarchicalInstancedStaticMeshComponent;

/** Serialized source placements, without a Blueprint construction script resetting instances on load. */
UCLASS()
class BROKENEDGE_API ASpaceCenterInstanceGroup : public AActor
{
	GENERATED_BODY()

public:
	ASpaceCenterInstanceGroup();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Space Center")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Instances;
};
