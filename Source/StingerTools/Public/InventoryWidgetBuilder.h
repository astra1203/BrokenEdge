#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "InventoryWidgetBuilder.generated.h"

/** Creates designer-editable inventory templates once; never replaces existing assets. */
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
