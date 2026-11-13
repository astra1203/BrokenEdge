#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ExtractionItemRegistry.generated.h"

class UExtractionItemDefinition;

/** Stable saved item identity is independent of the asset's package/name. */
UCLASS(BlueprintType)
class BROKENEDGE_API UExtractionItemRegistry : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TMap<FName, TSoftObjectPtr<UExtractionItemDefinition>> Entries;

    UFUNCTION(BlueprintCallable)
    TArray<FString> ValidateEntries() const;

    const UExtractionItemDefinition* Resolve(FName Id) const;
    static const UExtractionItemRegistry* Get();
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
