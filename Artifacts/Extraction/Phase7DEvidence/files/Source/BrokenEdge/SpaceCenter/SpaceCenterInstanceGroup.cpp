#include "SpaceCenterInstanceGroup.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"

ASpaceCenterInstanceGroup::ASpaceCenterInstanceGroup()
{
	PrimaryActorTick.bCanEverTick = false;
	Instances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Instances"));
	SetRootComponent(Instances);
	Instances->SetMobility(EComponentMobility::Static);
}
