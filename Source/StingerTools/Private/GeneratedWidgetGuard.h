#pragma once
#include "CoreMinimal.h"
class UWidgetBlueprint;

/** Editor-only protection. Sidecars are versioned with the generated assets. */
namespace GeneratedWidgetGuard
{
    using FSnapshot = TMap<FString, FString>;
    FSnapshot Snapshot(UWidgetBlueprint* Blueprint);
    FString Difference(const FSnapshot& Previous, const FSnapshot& Current);
    FString Preflight(const TArray<FString>& PackagePaths);
    bool Record(UWidgetBlueprint* Blueprint);
}
