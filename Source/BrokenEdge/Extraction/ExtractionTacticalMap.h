#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ExtractionTacticalMap.generated.h"

UCLASS()
class BROKENEDGE_API UExtractionTacticalMap : public UUserWidget
{
	GENERATED_BODY()
public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	UPROPERTY(Transient) TObjectPtr<class UExtractionMapDefinition> Definition;
};
