#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ExtractionNavigation.generated.h"

UCLASS()
class BROKENEDGE_API UExtractionNavigation : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Editor preparation only; does not rebuild runtime navigation. */
	UFUNCTION(BlueprintCallable, meta=(WorldContext="Context"))
	static bool BuildNavigation(UObject* Context);
};
