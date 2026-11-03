#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "InventoryWidgetBuilder.generated.h"

/** Builds the project's own inventory layout; existing templates are rebuilt in place at the same asset paths. */
UCLASS()
class STINGERTOOLS_API UInventoryWidgetBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Extraction|Editor")
	static FString CreateInventoryWidgets();
	UFUNCTION(BlueprintCallable, Category="Extraction|Editor")
	static FString ValidateInventoryWidgets();
	/** Renders disposable instances of the saved weapon cards; never changes inventory or assets. */
	UFUNCTION(BlueprintCallable, Category="Extraction|Editor")
	static FString RenderWeaponLayoutPreview();
};
