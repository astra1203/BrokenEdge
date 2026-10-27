#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/DragDropOperation.h"
#include "ExtractionItems.h"
#include "LootingMainView.generated.h"

class UCanvasPanel; class UVerticalBox; class UTextBlock; class UImage; class UBorder;
class USizeBox; class UButton; class USpinBox; class UProgressBar;
class ULootingMainView;

/** Blueprint-owned presentation; native code only supplies data and input behavior. */
UCLASS(Blueprintable)
class BROKENEDGE_API ULootingWidgetPart : public UUserWidget
{
 GENERATED_BODY()
public:
 UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UTextBlock> Label;
 UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UTextBlock> Detail;
 UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UImage> Icon;
 UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UBorder> RarityBar;
 UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UCanvasPanel> ItemCanvas;
 UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UCanvasPanel> GridCanvas;
 UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UVerticalBox> Entries;
 UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<USizeBox> ContentSize;
 UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UButton> ActionButton;
 UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<USpinBox> SplitAmount;
 UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UProgressBar> Progress;
 UPROPERTY(BlueprintReadOnly) FGuid ItemId;
 UPROPERTY(BlueprintReadOnly) bool bLoot = false;
 UPROPERTY(BlueprintReadOnly) int32 EquipmentSlot = -1;
 UPROPERTY(BlueprintReadOnly) int32 ActionIndex = -1;
 UPROPERTY(Transient) TObjectPtr<ULootingMainView> Inventory;
 FVector2D GrabFraction = FVector2D::ZeroVector;
 void SetItem(const FExtractionItem& Item);
 virtual void NativeOnInitialized() override;
 virtual FReply NativeOnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
 virtual void NativeOnDragDetected(const FGeometry&, const FPointerEvent&, UDragDropOperation*&) override;
 virtual bool NativeOnDrop(const FGeometry&, const FDragDropEvent&, UDragDropOperation*) override;
 virtual bool NativeOnDragOver(const FGeometry&, const FDragDropEvent&, UDragDropOperation*) override;
 UFUNCTION() void ClickAction();
};

UCLASS()
class BROKENEDGE_API ULootingDragDropOperation : public UDragDropOperation
{
 GENERATED_BODY()
public:
 UPROPERTY() TObjectPtr<ULootingMainView> Inventory;
 UPROPERTY(BlueprintReadOnly) FGuid ItemId;
 UPROPERTY(BlueprintReadOnly) bool bFromLoot = false;
 UPROPERTY(BlueprintReadOnly) bool bRotated = false;
 UPROPERTY(BlueprintReadOnly) int32 Quantity = 1;
 UPROPERTY(BlueprintReadOnly) FVector2D GrabFraction = FVector2D::ZeroVector;
 virtual void DragCancelled_Implementation(const FPointerEvent&) override;
};

/** Layout lives in WBP_LootingPickupView_Pc; all inventory mutations use existing RPCs. */
UCLASS(Blueprintable)
class BROKENEDGE_API ULootingMainView : public UUserWidget
{
 GENERATED_BODY()
public:
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Inventory|Layout") float CellSize = 64.f;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Inventory|Templates") TSubclassOf<ULootingWidgetPart> ItemClass;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Inventory|Templates") TSubclassOf<ULootingWidgetPart> CellClass;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Inventory|Templates") TSubclassOf<ULootingWidgetPart> SlotClass;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Inventory|Templates") TSubclassOf<ULootingWidgetPart> ActionClass;
 UPROPERTY(meta=(BindWidget), BlueprintReadOnly) TObjectPtr<UWidget> InventoryBody;
 UPROPERTY(meta=(BindWidget), BlueprintReadOnly) TObjectPtr<UCanvasPanel> PopupCanvas;
 UPROPERTY(meta=(BindWidget), BlueprintReadOnly) TObjectPtr<UVerticalBox> EquipmentList;
 UPROPERTY(meta=(BindWidget), BlueprintReadOnly) TObjectPtr<ULootingWidgetPart> OwnContainer;
 UPROPERTY(meta=(BindWidget), BlueprintReadOnly) TObjectPtr<ULootingWidgetPart> LootContainer;
 UPROPERTY(meta=(BindWidget), BlueprintReadOnly) TObjectPtr<ULootingWidgetPart> ContextMenu;
 UPROPERTY(meta=(BindWidget), BlueprintReadOnly) TObjectPtr<ULootingWidgetPart> SplitDialog;
 UPROPERTY(meta=(BindWidget), BlueprintReadOnly) TObjectPtr<ULootingWidgetPart> InteractionPrompt;
 UPROPERTY(meta=(BindWidget), BlueprintReadOnly) TObjectPtr<UTextBlock> StatusText;
 UPROPERTY(meta=(BindWidget), BlueprintReadOnly) TObjectPtr<UTextBlock> LootTitle;
 UPROPERTY(meta=(BindWidget), BlueprintReadOnly) TObjectPtr<UTextBlock> OwnTitle;
 void Refresh();
 bool CancelDrag();
 bool SelectItem(FGuid Id, bool Loot);
 void QuickTransfer();
 void ShowContext(FVector2D ScreenPosition);
 void MenuAction(int32 Index);
 void BeginDrag(ULootingWidgetPart* Source, UDragDropOperation*& Out);
 bool PlaceAt(FVector2D ScreenPosition, UDragDropOperation* Operation = nullptr);
 void UpdateDragRotation();
 bool HasPendingPlacement() const { return bPendingPlacement; }
 virtual void NativeOnInitialized() override;
 virtual FReply NativeOnKeyDown(const FGeometry&, const FKeyEvent&) override;
 virtual FReply NativeOnPreviewKeyDown(const FGeometry&, const FKeyEvent&) override;
 virtual FReply NativeOnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
 virtual bool NativeOnDrop(const FGeometry&, const FDragDropEvent&, UDragDropOperation*) override;
 virtual bool NativeOnDragOver(const FGeometry&, const FDragDropEvent&, UDragDropOperation*) override;
 virtual void NativeDestruct() override;
private:
 class AExtractionPlayerController* PC() const;
 class UExtractionInteractionComponent* Interaction() const;
 const FExtractionContainer* Container(bool Loot) const;
 FExtractionItemRequest Request() const;
 void EquipOrStore();
 void RebuildContainer(ULootingWidgetPart*, bool Loot);
 void RebuildEquipment(UVerticalBox*, bool Loot);
 ULootingWidgetPart* MakePart(TSubclassOf<ULootingWidgetPart> Class);
 void UpdatePrompt();
 void RotateDrag();
 UPROPERTY(Transient) TArray<TObjectPtr<ULootingWidgetPart>> EquipmentTargets;
 UPROPERTY(Transient) TObjectPtr<ULootingDragDropOperation> ActiveDrag;
 FGuid Selected;
 bool SelectedLoot=false, Rotated=false, bPendingPlacement=false, bWasOpen=false, bRotationDown=false;
 int32 Quantity=1;
 uint32 LastSession=0;
 FGuid LastOwnId, LastLootId;
 int32 LastOwnRevision=INDEX_NONE, LastLootRevision=INDEX_NONE;
};
