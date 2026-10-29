#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ExtractionBlueprintBuilder.generated.h"

/** One-time templates. Existing designer assets are validated, never rebuilt. */
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
