#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NativeReferenceAudit.generated.h"

/** Read-only package-header inspection for native class moves; never saves an asset. */
UCLASS()
class STINGERTOOLS_API UNativeReferenceAudit : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category = "Validation")
    static FString InspectPackage(const FString& Filename, const TArray<FString>& ClassPaths);
};
