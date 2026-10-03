#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionExit.generated.h"

UCLASS()
class BROKENEDGE_API AExtractionExit : public AActor
{
	GENERATED_BODY()
public:
	AExtractionExit();
	bool Contains(const FVector& Position) const;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Marker;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UTextRenderComponent> Label;
};
