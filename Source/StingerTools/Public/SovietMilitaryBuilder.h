#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SovietMilitaryBuilder.generated.h"

UCLASS()
class STINGERTOOLS_API USovietMilitaryBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable)
	static FString RepairWindows(bool Apply);
	UFUNCTION(BlueprintCallable)
	static bool BuildNavigation(UObject* WorldContext);
	/** Rebuild physics and navigation caches after changing a mesh collision trace policy. */
	UFUNCTION(BlueprintCallable)
	static bool RefreshCollision(class UStaticMesh* Mesh);
	UFUNCTION(BlueprintCallable)
	static int32 ConfigureBackdrop(float MinimumDistance);
	/** Ground position or invalid (NaN); tests floor, capsule clearance and optional nav projection. */
	UFUNCTION(BlueprintCallable)
	static FVector ProbeGround(UObject* WorldContext, FVector Position, bool RequireNavigation);
};
