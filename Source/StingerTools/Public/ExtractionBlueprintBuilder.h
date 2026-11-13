#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ExtractionBlueprintBuilder.generated.h"

/** Rebuilds the HUD only after generation baseline checks; existing loot actor defaults are preserved. */
UCLASS()
class STINGERTOOLS_API UExtractionBlueprintBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Extraction|Editor")
	static FString CreateExtractionBlueprints();
	/** Replace only exact native Loot instances in the current editor world. Does not save. */
	UFUNCTION(BlueprintCallable, Category="Extraction|Editor")
	static FString MigrateLootActors();
	UFUNCTION(BlueprintCallable, Category="Extraction|Editor")
	static FString ValidateExtractionBlueprints();
};
