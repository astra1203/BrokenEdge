#include "LootingMainView.h"
#include "ExtractionPlayerController.h"
#include "ExtractionInteractionComponent.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionLootActor.h"
#include "ExtractionKeyRoom.h"
#include "ExtractionExitSwitch.h"
#include "ExtractionZipline.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "ExtractionState.h"
#include "ExtractionGunsmith.h"
#include "ExtractionUIStyle.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/Button.h"
#include "Components/SpinBox.h"
#include "Components/ProgressBar.h"
#include "Engine/Texture2D.h"

namespace
{
	void Text(UTextBlock* Widget, const FString& Value) { if (Widget) Widget->SetText(FText::FromString(Value)); }

	bool Contains(UWidget* Widget, FVector2D Point)
	{
		return Widget && Widget->IsVisible() && Widget->GetCachedGeometry().IsUnderLocation(Point);
	}

	void CanvasChild(UCanvasPanel* Canvas, UWidget* Child, FVector2D At, FVector2D Size)
	{
		auto* Slot = Canvas->AddChildToCanvas(Child);
		Slot->SetPosition(At);
		Slot->SetSize(Size);
	}

	// 一块网格分区在画布上的位置；Top 以格为单位。
	struct FGridView
	{
		ExtractionItems::FGrid Grid;
		float Top = 0;
	};

	// 携行容器自上而下排胸挂、背包、安全箱，每段上方留半格写标题；固定网格（战利品、仓库）不留标题。
	TArray<FGridView> GridLayout(const FExtractionContainer& C, FVector2D& Extent)
	{
		TArray<FGridView> Views;
		float Y = 0;
		int32 Width = 1;
		for (const auto& Grid : ExtractionItems::Grids(C))
		{
			if (C.GearGrids) Y += .5f;
			Views.Add({Grid, Y});
			Y += Grid.Rows() + (C.GearGrids ? .25f : 0.f);
			Width = FMath::Max(Width, Grid.Width);
		}
		Extent = FVector2D(Width, FMath::Max(Y, 1.f));
		return Views;
	}

	FString GridTitle(const FExtractionContainer& C, const ExtractionItems::FGrid& Grid)
	{
		const auto* Gear = C.InSlot(ExtractionGear::SlotForGrid(Grid.Section));
		const auto* D = Gear ? UExtractionItemDefinition::Get(Gear->Definition) : nullptr;
		const TCHAR* Kind = Grid.Section == ExtractionGear::RigGrid
			                    ? TEXT("胸挂")
			                    : Grid.Section == ExtractionGear::SafeGrid
			                    ? TEXT("安全箱 · 死亡不掉落")
			                    : TEXT("背包");
		return FString::Printf(TEXT("%s  %s  %d 格"), Kind, D ? *D->Label.ToString() : TEXT(""), Grid.Cells);
	}

	// 屏幕局部坐标（格）落在哪个分区：标题行算在下方分区里，方便把东西拖到分区顶部。
	const FGridView* GridAt(const TArray<FGridView>& Views, float LocalY)
	{
		for (const auto& View : Views)
			if (LocalY >= View.Top - .5f && LocalY < View.Top + View.Grid.Rows() + .25f) return &View;
		return nullptr;
	}
}

void ULootingWidgetPart::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	ExtractionUIStyle::ApplyFont(this);
	SetIsFocusable(true);
	if (ActionButton) ActionButton->OnClicked.AddDynamic(this, &ThisClass::ClickAction);
}

void ULootingWidgetPart::ClickAction() { if (Inventory) Inventory->MenuAction(ActionIndex); }

void ULootingWidgetPart::SetBaseColor(FLinearColor Color)
{
	BaseColor = Color;
	ApplyBackground();
}

void ULootingWidgetPart::ApplyBackground()
{
	if (!Background || BaseColor.A <= 0) return;
	FLinearColor Color = BaseColor;
	if (bHovered) Color = FMath::Lerp(Color, FLinearColor::White, .08f).CopyWithNewOpacity(BaseColor.A);
	Background->SetBrushColor(Color);
}

void ULootingWidgetPart::NativeOnMouseEnter(const FGeometry& G, const FPointerEvent& E)
{
	Super::NativeOnMouseEnter(G, E);
	bHovered = true;
	ApplyBackground();
}

void ULootingWidgetPart::NativeOnMouseLeave(const FPointerEvent& E)
{
	Super::NativeOnMouseLeave(E);
	bHovered = false;
	ApplyBackground();
}

void ULootingWidgetPart::SetItem(const FExtractionItem& Item)
{
	ItemId = Item.Id;
	const auto* D = UExtractionItemDefinition::Get(Item.Definition);
	auto* Texture = Icon && D && !D->Icon.IsNull() ? D->Icon.LoadSynchronous() : nullptr;
	// 有图标时只显示图标（名称放进悬停提示），没有图标的物品退回显示名称。
	// 未揭示的物品只显示问号，搜索完成后才露出图标。
	Text(Label, D ? D->Label.ToString() : TEXT("?"));
	if (Label) Label->SetVisibility(Texture ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	const bool bWeapon = D && D->Kind == EExtractionItemKind::Weapon;
	// 枪的剪影彼此相近，角上同时写枪名，一眼能认出是哪把。
	Text(Detail, bWeapon ? FString::Printf(TEXT("%s  %d/%d"), *D->Label.ToString(), Item.Magazine,
	                                       D->MagazineCapacity)
		             : FString::Printf(TEXT("×%d"), Item.Quantity));
	if (Detail)
		Detail->SetVisibility(bWeapon || Item.Quantity > 1 ? ESlateVisibility::HitTestInvisible
			                      : ESlateVisibility::Collapsed);
	if (Icon)
	{
		if (Texture)
		{
			// 按物品占格尺寸等比缩放，长枪图标不会被压扁。
			const float CellPixels = Inventory ? Inventory->CellSize : 64.f;
			const FVector2D Area = FVector2D(Item.Extent()) * CellPixels * .9f;
			const float Fit = FMath::Min(Area.X / FMath::Max(1, Texture->GetSizeX()),
			                             Area.Y / FMath::Max(1, Texture->GetSizeY()));
			FSlateBrush Brush;
			Brush.SetResourceObject(Texture);
			Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY()) * Fit;
			Icon->SetBrush(Brush);
		}
		Icon->SetVisibility(Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	const FLinearColor Rarity = ExtractionUIStyle::Rarity(D ? D->DisplayRarity : 0);
	if (RarityBar)
	{
		RarityBar->SetBrushColor(Rarity);
		RarityBar->SetVisibility(D ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	// 底板按品质轻微着色：深色打底，品质越高颜色越明显。
	SetBaseColor(D
		             ? FMath::Lerp(ExtractionUIStyle::Hex(TEXT("1A1E24")), Rarity, .14f + .03f * D->DisplayRarity).
		             CopyWithNewOpacity(.96f)
		             : ExtractionUIStyle::Hex(TEXT("15181D"), .96f));
	if (Item.Id.IsValid())
	{
		auto Lines = ExtractionItems::WeaponDetails(Item);
		if (D && D->SellValue > 0)
		{
			// 总价值之外再给每格价值，背包满时方便比较该留哪件。
			const int64 Total = int64(D->SellValue) * FMath::Max(1, Item.Quantity);
			const int32 Cells = FMath::Max(1, Item.Size.X * Item.Size.Y);
			Lines.Insert(FString::Printf(TEXT("价值 ¥%s  ·  每格 ¥%s"), *FText::AsNumber(Total).ToString(),
			                             *FText::AsNumber(Total / Cells).ToString()), 0);
		}
		if (D) Lines.Insert(D->Label.ToString(), 0);
		SetToolTipText(FText::FromString(FString::Join(Lines,TEXT("\n"))));
	}
	else SetToolTipText(FText::GetEmpty());
}

FReply ULootingWidgetPart::NativeOnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	if (!Inventory) return Super::NativeOnMouseButtonDown(G, E);
	if (Inventory->HasPendingPlacement() && E.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		Inventory->PlaceAt(E.GetScreenSpacePosition());
		return FReply::Handled();
	}
	if (!ItemId.IsValid() || !Inventory->SelectItem(ItemId, bLoot)) return Super::NativeOnMouseButtonDown(G, E);
	if (E.GetEffectingButton() == EKeys::RightMouseButton)
	{
		Inventory->ShowContext(E.GetScreenSpacePosition());
		return FReply::Handled().SetUserFocus(Inventory->TakeWidget());
	}
	if (E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	if (E.IsShiftDown())
	{
		Inventory->QuickTransfer();
		return FReply::Handled();
	}
	const FVector2D Size = G.GetLocalSize();
	const FVector2D P = G.AbsoluteToLocal(E.GetScreenSpacePosition());
	GrabFraction = FVector2D(P.X / FMath::Max(1.f, Size.X), P.Y / FMath::Max(1.f, Size.Y));
	return UWidgetBlueprintLibrary::DetectDragIfPressed(E, this, EKeys::LeftMouseButton).NativeReply;
}

FReply ULootingWidgetPart::NativeOnMouseButtonDoubleClick(const FGeometry& G, const FPointerEvent& E)
{
	if (!Inventory || !ItemId.IsValid() || E.GetEffectingButton() != EKeys::LeftMouseButton
		|| !Inventory->SelectItem(ItemId, bLoot))
		return Super::NativeOnMouseButtonDoubleClick(G, E);
	Inventory->QuickTransfer();
	return FReply::Handled();
}

void ULootingWidgetPart::NativeOnDragDetected(const FGeometry&, const FPointerEvent&, UDragDropOperation*& Out)
{
	if (Inventory && ItemId.IsValid()) Inventory->BeginDrag(this, Out);
}

bool ULootingWidgetPart::NativeOnDrop(const FGeometry&, const FDragDropEvent& E, UDragDropOperation* Op)
{
	return Inventory && Inventory->PlaceAt(E.GetScreenSpacePosition(), Op);
}

bool ULootingWidgetPart::NativeOnDragOver(const FGeometry&, const FDragDropEvent& E, UDragDropOperation*)
{
	if (Inventory) Inventory->DragOver(E.GetScreenSpacePosition());
	return Inventory != nullptr;
}

void ULootingDragDropOperation::DragCancelled_Implementation(const FPointerEvent& E)
{
	Super::DragCancelled_Implementation(E);
	if (Inventory) Inventory->CancelDrag();
}

AExtractionPlayerController* ULootingMainView::PC() const
{
	return Cast<AExtractionPlayerController>(GetOwningPlayer());
}

UExtractionInteractionComponent* ULootingMainView::Interaction() const
{
	return PC() ? PC()->Interaction() : nullptr;
}

const FExtractionContainer* ULootingMainView::Container(bool Loot) const
{
	auto* I = Interaction();
	return !I || !I->Inventory() ? nullptr : Loot ? I->Loot ? &I->LootView : nullptr : &I->Inventory()->Contents;
}

ULootingWidgetPart* ULootingMainView::MakePart(TSubclassOf<ULootingWidgetPart> Class)
{
	if (!Class) return nullptr;
	auto* Part = CreateWidget<ULootingWidgetPart>(GetOwningPlayer(), Class);
	Part->Inventory = this;
	return Part;
}

void ULootingMainView::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	ExtractionUIStyle::ApplyFont(this);
	SetIsFocusable(true);
	for (auto* Part : {
		     OwnContainer.Get(), LootContainer.Get(), ContextMenu.Get(), SplitDialog.Get(), InteractionPrompt.Get()
	     })
		if (Part) Part->Inventory = this;
	if (SplitDialog) SplitDialog->ActionIndex = -2;
	if (SortButton) SortButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SortOwn);
	if (TakeAllButton) TakeAllButton->OnClicked.AddUniqueDynamic(this, &ThisClass::TakeAllLoot);
	if (ContextMenu) ContextMenu->SetVisibility(ESlateVisibility::Collapsed);
	if (SplitDialog) SplitDialog->SetVisibility(ESlateVisibility::Collapsed);
}

void ULootingMainView::NativeDestruct()
{
	CancelDrag();
	Super::NativeDestruct();
}

bool ULootingMainView::CancelDrag()
{
	const bool Busy = Selected.IsValid() || ActiveDrag || bPendingPlacement || (ContextMenu && ContextMenu->IsVisible())
		|| (SplitDialog && SplitDialog->IsVisible());
	auto* Drag = ActiveDrag.Get();
	ActiveDrag = nullptr;
	if (Drag)
	{
		Drag->Inventory = nullptr;
		UWidgetBlueprintLibrary::CancelDragDrop();
	}
	Selected.Invalidate();
	bPendingPlacement = false;
	HidePreview();
	if (ContextMenu) ContextMenu->SetVisibility(ESlateVisibility::Collapsed);
	if (SplitDialog) SplitDialog->SetVisibility(ESlateVisibility::Collapsed);
	return Busy;
}

bool ULootingMainView::SelectItem(FGuid Id, bool Loot)
{
	const auto* I = Interaction();
	const auto* C = Container(Loot);
	const auto* Item = C ? C->Find(Id) : nullptr;
	if (!I || !I->InventoryOpen || !I->CanAct() || !Id.IsValid() || !Item) return false;
	CancelDrag();
	Selected = Id;
	SelectedLoot = Loot;
	Quantity = Item->Quantity;
	Rotated = Item->Rotated;
	return true;
}

FExtractionItemRequest ULootingMainView::Request() const
{
	FExtractionItemRequest R;
	R.Item = Selected;
	R.FromLoot = SelectedLoot;
	R.Quantity = Quantity;
	R.Rotated = Rotated;
	return R;
}

void ULootingMainView::RebuildEquipment(UVerticalBox* List, bool Loot)
{
	if (!List) return;
	List->ClearChildren();
	EquipmentTargets.RemoveAll([Loot](const auto& P) { return !P || P->bLoot == Loot; });
	const auto* C = Container(Loot);
	if (!C || !C->WeaponSlots) return;
	const TCHAR* Names[] = {
		TEXT("手持武器"), TEXT("背负武器"), TEXT("头盔"), TEXT("胸甲"), TEXT("胸挂"), TEXT("背包"), TEXT("安全箱")
	};
	// 携行容器多出胸挂、背包、安全箱三个槽；守卫等固定网格容器仍只有前四个。
	// 两把武器各占一整行，其余装备两个一行。
	const int32 SlotCount = C->GearGrids ? ExtractionGear::SafeSlot + 1 : 4;
	UHorizontalBox* Row = nullptr;
	for (int32 N = 0; N < SlotCount; ++N)
	{
		auto* EquipmentView = MakePart(SlotClass);
		if (!EquipmentView) continue;
		EquipmentView->bLoot = Loot;
		EquipmentView->EquipmentSlot = N;
		Text(EquipmentView->Label, Names[N]);
		EquipmentView->SetBaseColor(ExtractionUIStyle::Hex(TEXT("12151A"), .92f));
		if (EquipmentView->ContentSize) EquipmentView->ContentSize->SetHeightOverride(N < 2 ? 128 : 100);
		if (N < 2) List->AddChildToVerticalBox(EquipmentView)->SetPadding(FMargin(0, 0, 0, 10));
		else
		{
			if (!Row || Row->GetChildrenCount() == 2)
			{
				Row = WidgetTree->ConstructWidget<UHorizontalBox>();
				List->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 0, 0, 10));
			}
			const bool bLeft = Row->GetChildrenCount() == 0;
			auto* RowSlot = Row->AddChildToHorizontalBox(EquipmentView);
			RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			RowSlot->SetPadding(FMargin(bLeft ? 0 : 5, 0, bLeft ? 5 : 0, 0));
		}
		EquipmentTargets.Add(EquipmentView);
		if (const auto* Item = C->InSlot(N); Item && EquipmentView->ItemCanvas)
		{
			auto* Card = MakePart(ItemClass);
			if (!Card) continue;
			Card->bLoot = Loot;
			Card->SetItem(*Item);
			auto* S = EquipmentView->ItemCanvas->AddChildToCanvas(Card);
			S->SetAnchors(FAnchors(0, 0, 1, 1));
			S->SetOffsets(FMargin(0));
		}
	}
}

void ULootingMainView::RebuildContainer(ULootingWidgetPart* Panel, bool Loot)
{
	if (!Panel || !Panel->ItemCanvas || !Panel->GridCanvas) return;
	Panel->bLoot = Loot;
	Panel->ItemCanvas->ClearChildren();
	Panel->GridCanvas->ClearChildren();
	const auto* C = Container(Loot);
	if (Loot && Panel->Entries) RebuildEquipment(Panel->Entries, true);
	if (!C)
	{
		if (Panel->ContentSize)
		{
			Panel->ContentSize->SetWidthOverride(1);
			Panel->ContentSize->SetHeightOverride(1);
		}
		return;
	}
	FVector2D Extent;
	const auto Views = GridLayout(*C, Extent);
	const FVector2D Size = Extent * CellSize;
	if (Panel->ContentSize)
	{
		Panel->ContentSize->SetWidthOverride(Size.X);
		Panel->ContentSize->SetHeightOverride(Size.Y);
	}
	for (const auto& View : Views)
	{
		if (C->GearGrids && WidgetTree)
			if (auto* Title = WidgetTree->ConstructWidget<UTextBlock>())
			{
				Text(Title, GridTitle(*C, View.Grid));
				Title->SetColorAndOpacity(FSlateColor(ExtractionUIStyle::Muted()));
				FSlateFontInfo Font = Title->GetFont();
				Font.Size = FMath::Max(9, FMath::RoundToInt(CellSize * .22f));
				Title->SetFont(Font);
				CanvasChild(Panel->GridCanvas, Title, FVector2D(0, (View.Top - .5f) * CellSize),
				            FVector2D(Extent.X * CellSize, CellSize * .5f));
			}
		for (int32 Y = 0; Y < View.Grid.Rows(); ++Y)
			for (int32 X = 0; X < View.Grid.Width; ++X)
				if (View.Grid.Has(X, Y))
					if (auto* GridPart = MakePart(CellClass))
					{
						GridPart->SetVisibility(ESlateVisibility::HitTestInvisible);
						CanvasChild(Panel->GridCanvas, GridPart, FVector2D(X, View.Top + Y) * CellSize,
						            FVector2D(CellSize - 2));
					}
		// 分仓：每个口袋描一圈琥珀细边，相邻口袋一眼能分开。
		if (WidgetTree)
			for (const FIntRect& Pocket : View.Grid.Pockets)
				if (auto* Frame = WidgetTree->ConstructWidget<UBorder>())
				{
					Frame->SetBrush(ExtractionUIStyle::Panel(FLinearColor::Transparent, 3.f, ExtractionUIStyle::Accent(.55f)));
					Frame->SetVisibility(ESlateVisibility::HitTestInvisible);
					CanvasChild(Panel->GridCanvas, Frame, FVector2D(Pocket.Min.X, View.Top + Pocket.Min.Y) * CellSize - FVector2D(1),
					            FVector2D(Pocket.Width(), Pocket.Height()) * CellSize);
				}
	}
	for (const auto& Item : C->Items)
	{
		const auto* View = Views.FindByPredicate([&](const FGridView& V) { return V.Grid.Section == Item.Slot; });
		if (Item.Slot >= 0 || !View) continue;
		if (auto* Card = MakePart(ItemClass))
		{
			Card->bLoot = Loot;
			Card->SetItem(Item);
			CanvasChild(Panel->ItemCanvas, Card, FVector2D(Item.Cell.X, View->Top + Item.Cell.Y) * CellSize,
			            FVector2D(Item.Extent()) * CellSize - FVector2D(2));
		}
	}
}

void ULootingMainView::Refresh()
{
	if (!InventoryBody || !OwnContainer || !LootContainer) return;
	auto* I = Interaction();
	const bool Open = I && I->InventoryOpen && I->CanAct();
	if (!Open || (I && LastSession != I->Session)) CancelDrag();
	if (Selected.IsValid())
	{
		const auto* C = Container(SelectedLoot);
		if (!C || !C->Find(Selected)) CancelDrag();
	}
	InventoryBody->SetVisibility(Open ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (InteractionPrompt)
		InteractionPrompt->SetVisibility(
			!Open && I && I->CanAct() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (Open)
	{
		const auto* Own = Container(false);
		const auto* Loot = Container(true);
		if (I->Loot) SearchedLoot.Add(I->Loot.Get());
		if (Own && (!bWasOpen || Own->Id != LastOwnId || Own->Revision != LastOwnRevision))
		{
			RebuildEquipment(EquipmentList, false);
			RebuildContainer(OwnContainer, false);
			LastOwnId = Own->Id;
			LastOwnRevision = Own->Revision;
		}
		// 搜出一件物品时服务器只更新快照里的揭示内容、不改容器版本，所以还要比较已揭示件数。
		int32 RevealedCount = 0;
		if (Loot) for (const auto& Item : Loot->Items) RevealedCount += Item.Definition.IsNone() ? 0 : 1;
		if (!bWasOpen || (Loot ? Loot->Id : FGuid()) != LastLootId || (Loot ? Loot->Revision : INDEX_NONE) !=
			LastLootRevision || RevealedCount != LastLootRevealed)
		{
			RebuildContainer(LootContainer, true);
			LastLootId = Loot ? Loot->Id : FGuid();
			LastLootRevision = Loot ? Loot->Revision : INDEX_NONE;
			LastLootRevealed = RevealedCount;
		}
		int32 OwnCells = 0, UsedCells = 0;
		int64 Value = 0;
		if (Own)
		{
			for (const auto& Grid : ExtractionItems::Grids(*Own)) OwnCells += Grid.Cells;
			for (const auto& Item : Own->Items)
			{
				if (Item.Slot < 0) UsedCells += Item.Extent().X * Item.Extent().Y;
				if (const auto* D = UExtractionItemDefinition::Get(Item.Definition))
					Value += int64(D->SellValue) * FMath::Max(1, Item.Quantity);
			}
		}
		Text(OwnTitle, TEXT("携行"));
		Text(ValueText, FString::Printf(TEXT("携行价值  ¥%s      已用 %d / %d 格"), *FText::AsNumber(Value).ToString(),
		                                UsedCells, OwnCells));
		const auto* GS = GetWorld()->GetGameState<AExtractionGameState>();
		const double Now = GS ? GS->GetServerWorldTimeSeconds() : 0;
		const bool bSearching = I->Loot && I->SearchEnd > Now;
		// 未揭示的物品在客户端快照里没有定义，据此统计还剩几件没搜。
		int32 Hidden = 0;
		if (Loot) for (const auto& Item : Loot->Items) Hidden += Item.Definition.IsNone() ? 1 : 0;
		FString LootLabel = I->Loot ? I->Loot->Label : TEXT("搜索容器以查看物资");
		if (I->Loot && bSearching) LootLabel += TEXT("  ·  搜索中");
		if (I->Loot && Hidden > 0) LootLabel += FString::Printf(TEXT("  ·  还有 %d 件未搜"), Hidden);
		else if (I->Loot && Loot && !Loot->Items.IsEmpty()) LootLabel += TEXT("  ·  已搜完");
		Text(LootTitle, LootLabel);
		if (SearchProgress)
		{
			// 每件物品单独计时：看到新的结束时间就从当前时刻重新开始走条。
			if (bSearching && I->SearchEnd != SearchSeen)
			{
				SearchSeen = I->SearchEnd;
				SearchStart = Now;
			}
			SearchProgress->SetVisibility(bSearching ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
			SearchProgress->SetPercent(bSearching ? (Now - SearchStart) / FMath::Max(.05, SearchSeen - SearchStart) : 0);
		}
		Text(StatusText, bPendingPlacement ? TEXT("选择目标格子放置拆分物品 · R 旋转 · Esc 取消") : PC()->ItemMessage);
		if (!bWasOpen) SetUserFocus(GetOwningPlayer());
		UpdateDragRotation();
	}
	else UpdatePrompt();
	bWasOpen = Open;
	LastSession = I ? I->Session : 0;
}

void ULootingMainView::UpdatePrompt()
{
	if (!InteractionPrompt) return;
	auto* I = Interaction();
	if (!I || !I->CanAct()) return;
	FString Prompt;
	if (I->ZiplineFocus) Prompt = I->ZiplineFocus->PromptText(Cast<ABrokenEdgeCharacter>(GetOwningPlayerPawn()));
	else if (I->SwitchFocus) Prompt = I->SwitchFocus->PromptText();
	else if (I->DoorFocus)
	{
		auto* Door = I->DoorFocus.Get();
		int32 Uses = 0;
		if (const auto* C = Container(false))
			for (const auto& Item : C->Items)
				if (const auto* D = UExtractionItemDefinition::Get(Item.Definition); D && (Door->RequiredKey.IsNone()
						? Item.Definition == TEXT("Key")
						: D->KeyRoomId == Door->RequiredKey))
					Uses += FMath::Max(1, FMath::RoundToInt(Item.Durability));
		const FString Room = Door->RoomLabel.IsEmpty() ? TEXT("上锁房间") : Door->RoomLabel.ToString();
		Prompt = Uses > 0
			         ? FString::Printf(TEXT("F  %s · 开门 · 剩余 %d 次"), *Room, Uses)
			         : Room + TEXT(" · 需要 ") + (Door->RequiredCardLabel.IsEmpty()
				                                    ? TEXT("对应钥匙卡")
				                                    : Door->RequiredCardLabel.ToString());
	}
	else if (I->Focus)
	{
		FString Label = I->Focus->Label;
		if (I->Focus->Loose)
			if (const auto* D = UExtractionItemDefinition::Get(I->Focus->LooseDefinition))
				Label = D->
				        Label.ToString();
		Prompt = TEXT("E  ") + Label + (I->Focus->Loose
			                                ? TEXT(" · 拾取")
			                                : SearchedLoot.Contains(I->Focus.Get())
			                                ? TEXT(" · 再次查看（已搜过）")
			                                : TEXT(" · 搜索"));
	}
	const auto* GS = GetWorld()->GetGameState<AExtractionGameState>();
	const double Now = GS ? GS->GetServerWorldTimeSeconds() : 0;
	const float Remaining = FMath::Max(0., I->MedicalEnd - Now);
	if (I->MedicalEnd > 0) Prompt += FString::Printf(TEXT("\n治疗中 %.1fs · H / Esc 取消"), Remaining);
	Text(InteractionPrompt->Label, Prompt);
	Text(InteractionPrompt->Detail, PC()->ItemMessage);
	if (InteractionPrompt->Progress)
	{
		InteractionPrompt->Progress->SetVisibility(I->MedicalEnd > 0
			                                           ? ESlateVisibility::HitTestInvisible
			                                           : ESlateVisibility::Collapsed);
		InteractionPrompt->Progress->SetPercent(I->MedicalDuration > 0 ? 1 - Remaining / I->MedicalDuration : 0);
	}
}

void ULootingMainView::ShowContext(FVector2D Screen)
{
	if (!ContextMenu || !ContextMenu->Entries || !PopupCanvas) return;
	ContextMenu->Entries->ClearChildren();
	TArray<FString> Labels = {TEXT("快捷转移"),TEXT("装备 / 卸下"),TEXT("使用"),TEXT("拆分数量…"),TEXT("丢弃")};
	if (!SelectedLoot)
		if (const auto* C = Container(false))
			for (
				const auto& A : ExtractionItems::EquipmentActions(*C, Selected))
				Labels.Add(A.Label);
	for (int32 N = 0; N < Labels.Num(); ++N)
		if (auto* Row = MakePart(ActionClass))
		{
			Row->ActionIndex = N;
			Text(Row->Label, Labels[N]);
			ContextMenu->Entries->AddChildToVerticalBox(Row);
		}
	const auto& G = PopupCanvas->GetCachedGeometry();
	FVector2D P = G.AbsoluteToLocal(Screen);
	const FVector2D Limit = G.GetLocalSize() - FVector2D(240, Labels.Num() * 40 + 12);
	P.X = FMath::Clamp(P.X, 0., FMath::Max(0., Limit.X));
	P.Y = FMath::Clamp(P.Y, 0., FMath::Max(0., Limit.Y));
	if (auto* S = Cast<UCanvasPanelSlot>(ContextMenu->Slot)) S->SetPosition(P);
	ContextMenu->SetVisibility(ESlateVisibility::Visible);
}

void ULootingMainView::SortOwn()
{
	if (!PC() || !Interaction() || !Interaction()->InventoryOpen) return;
	CancelDrag();
	FExtractionItemRequest R;
	R.Op = EExtractionItemOp::Sort;
	PC()->RequestItem(R);
}

void ULootingMainView::TakeAllLoot()
{
	if (!PC() || !Interaction() || !Interaction()->InventoryOpen || !Interaction()->Loot) return;
	CancelDrag();
	FExtractionItemRequest R;
	R.Op = EExtractionItemOp::TakeAll;
	R.FromLoot = true;
	PC()->RequestItem(R);
}

void ULootingMainView::QuickTransfer()
{
	if (!PC() || !Interaction()) return;
	auto R = Request();
	R.ToLoot = !SelectedLoot && Interaction()->Loot != nullptr;
	if (!SelectedLoot && !R.ToLoot)
	{
		EquipOrStore();
		return;
	}
	PC()->RequestItem(R);
	CancelDrag();
}

void ULootingMainView::EquipOrStore()
{
	const auto* C = Container(SelectedLoot);
	const auto* Item = C ? C->Find(Selected) : nullptr;
	const auto* D = Item ? UExtractionItemDefinition::Get(Item->Definition) : nullptr;
	if (D && D->Kind == EExtractionItemKind::Gear)
	{
		// 胸挂/背包/安全箱直接放进对应装备槽；槽已占用时由服务器拒绝并提示。
		auto R = Request();
		R.ToLoot = false;
		R.Slot = D->GearSlot;
		PC()->RequestItem(R);
		CancelDrag();
		return;
	}
	if (!D || D->Kind != EExtractionItemKind::Weapon)
	{
		PC()->ShowItemMessage(TEXT("只有武器和携行装备可以装备"));
		CancelDrag();
		return;
	}
	auto R = Request();
	R.ToLoot = false;
	if (SelectedLoot || Item->Slot < 0)
	{
		const auto* Own = Container(false);
		if (!Own) return;
		R.Slot = !Own->InSlot(0) ? 0 : !Own->InSlot(1) ? 1 : -1;
		if (R.Slot < 0)
		{
			PC()->ShowItemMessage(TEXT("武器槽已满，请先移出原武器"));
			CancelDrag();
			return;
		}
	}
	PC()->RequestItem(R);
	CancelDrag();
}

void ULootingMainView::MenuAction(int32 Row)
{
	if (!PC() || !Interaction() || !Interaction()->InventoryOpen)
	{
		CancelDrag();
		return;
	}
	const auto* C = Container(SelectedLoot);
	const auto* Item = C ? C->Find(Selected) : nullptr;
	if (!Item)
	{
		CancelDrag();
		return;
	}
	if (Row == -2)
	{
		Quantity = FMath::Clamp(FMath::RoundToInt(SplitDialog->SplitAmount->GetValue()), 1,
		                        FMath::Max(1, Item->Quantity - 1));
		SplitDialog->SetVisibility(ESlateVisibility::Collapsed);
		bPendingPlacement = true;
		SetUserFocus(GetOwningPlayer());
		return;
	}
	ContextMenu->SetVisibility(ESlateVisibility::Collapsed);
	if (Row >= 5)
	{
		const auto Actions = SelectedLoot
			                     ? TArray<ExtractionItems::FEquipmentAction>()
			                     : ExtractionItems::EquipmentActions(*C, Selected);
		if (Actions.IsValidIndex(Row - 5))
		{
			const auto& A = Actions[Row - 5];
			if (A.OpenGunsmith)
			{
				TWeakObjectPtr<ULootingMainView> Weak(this);
				ExtractionGunsmith::Open(TakeWidget(), Selected, [Weak]
				                         {
					                         const auto* W = Weak.Get();
					                         const auto* Data = W && W->Interaction() && W->Interaction()->InventoryOpen
						                                            ? W->Container(false)
						                                            : nullptr;
					                         return Data ? *Data : FExtractionContainer();
				                         }, [Weak](const FExtractionItemRequest& R)
				                         {
					                         if (auto* W = Weak.Get(); W && W->PC()) W->PC()->RequestItem(R);
				                         });
			}
			else PC()->RequestItem(A.Request);
		}
		CancelDrag();
		return;
	}
	if (Row == 0)
	{
		QuickTransfer();
		return;
	}
	if (Row == 1)
	{
		EquipOrStore();
		return;
	}
	if (Row == 3)
	{
		if (Item->Quantity <= 1)
		{
			PC()->ShowItemMessage(TEXT("该物品无法拆分"));
			CancelDrag();
			return;
		}
		if (SplitDialog && SplitDialog->SplitAmount)
		{
			SplitDialog->SplitAmount->SetMinValue(1);
			SplitDialog->SplitAmount->SetMaxValue(Item->Quantity - 1);
			SplitDialog->SplitAmount->SetValue(Item->Quantity / 2);
			SplitDialog->SetVisibility(ESlateVisibility::Visible);
			SplitDialog->SplitAmount->SetUserFocus(GetOwningPlayer());
		}
		return;
	}
	auto R = Request();
	R.Op = Row == 2 ? EExtractionItemOp::Use : EExtractionItemOp::Drop;
	PC()->RequestItem(R);
	CancelDrag();
}

void ULootingMainView::BeginDrag(ULootingWidgetPart* Source, UDragDropOperation*& Out)
{
	if (!SelectItem(Source->ItemId, Source->bLoot)) return;
	auto* Op = NewObject<ULootingDragDropOperation>(this);
	Op->Inventory = this;
	Op->ItemId = Selected;
	Op->bFromLoot = SelectedLoot;
	Op->bRotated = Rotated;
	Op->Quantity = Quantity;
	Op->GrabFraction = Source->GrabFraction;
	if (auto* Visual = MakePart(ItemClass))
	{
		const auto* Item = Container(SelectedLoot)->Find(Selected);
		Visual->SetItem(*Item);
		Visual->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (Visual->ContentSize)
		{
			Visual->ContentSize->SetWidthOverride(Item->Extent().X * CellSize);
			Visual->ContentSize->SetHeightOverride(Item->Extent().Y * CellSize);
		}
		Op->DefaultDragVisual = Visual;
	}
	// 按抓取点在物品上的比例摆放拖拽图像，和落点换算（ResolveDrop）用同一个比例；
	// MouseDown 用源控件的像素偏移，源控件（装备槽、旋转过的格子）尺寸和拖拽图像不同时会错开。
	Op->Pivot = EDragPivot::TopLeft;
	Op->Offset = -Source->GrabFraction;
	ActiveDrag = Op;
	Out = Op;
	SetUserFocus(GetOwningPlayer());
}

void ULootingMainView::RotateDrag()
{
	if (!ActiveDrag && !bPendingPlacement) return;
	Rotated = !Rotated;
	if (ActiveDrag)
	{
		ActiveDrag->bRotated = Rotated;
		if (auto* V = Cast<ULootingWidgetPart>(ActiveDrag->DefaultDragVisual))
			if (const auto* C = Container(SelectedLoot))
				if (const auto* Item = C->Find(Selected))
				{
					const FIntPoint Ext = Rotated ? FIntPoint(Item->Size.Y, Item->Size.X) : Item->Size;
					if (V->ContentSize)
					{
						V->ContentSize->SetWidthOverride(Ext.X * CellSize);
						V->ContentSize->SetHeightOverride(Ext.Y * CellSize);
					}
				}
	}
}

void ULootingMainView::UpdateDragRotation()
{
	if (!PC()) return;
	const bool Down = PC()->IsInputKeyDown(EKeys::R);
	if (Down && !bRotationDown) RotateDrag();
	bRotationDown = Down;
}

// 把鼠标落点换算成放置请求；只读，不提交。装备槽优先于容器网格命中。
bool ULootingMainView::ResolveDrop(FVector2D Screen, FExtractionItemRequest& R, UCanvasPanel*& Canvas, FVector2D& At,
                                   FVector2D& Size) const
{
	const auto* C = Container(SelectedLoot);
	const auto* Item = C ? C->Find(Selected) : nullptr;
	if (!Item) return false;
	R = Request();
	// 使用鼠标指定的目标槽位或格子，关闭自动寻找空位。
	R.AutoPlace = false;
	for (const auto& Target : EquipmentTargets)
		if (Contains(Target, Screen) && Target->ItemCanvas)
		{
			R.ToLoot = Target->bLoot;
			R.Slot = Target->EquipmentSlot;
			Canvas = Target->ItemCanvas;
			At = FVector2D::ZeroVector;
			Size = Canvas->GetCachedGeometry().GetLocalSize();
			return true;
		}
	// 依次检查自身容器和战利品容器，只处理鼠标所在的物品画布。
	for (int32 Side = 0; Side < 2; ++Side)
	{
		auto* Panel = Side ? LootContainer.Get() : OwnContainer.Get();
		const auto* Target = Container(Side != 0);
		if (!Panel || !Target || !Contains(Panel->ItemCanvas, Screen)) continue;
		// 将屏幕位置转换为画布局部坐标，再按格子尺寸换算为网格单位。
		const FVector2D Local = Panel->ItemCanvas->GetCachedGeometry().AbsoluteToLocal(Screen) / CellSize;
		FVector2D Extent;
		const auto Views = GridLayout(*Target, Extent);
		// 按纵向位置定位具体分区。
		const auto* View = GridAt(Views, Local.Y);
		if (!View) return false;
		// 换算到该分区自己的格子坐标；松手位置减去抓取点得到物品左上角。
		const FVector2D InGrid(Local.X, Local.Y - View->Top);
		// 旋转后交换宽高，并将抓取比例换算为格子偏移；点击放置时偏移为零。
		const FIntPoint Ext = Rotated ? FIntPoint(Item->Size.Y, Item->Size.X) : Item->Size;
		const FVector2D Offset = ActiveDrag ? ActiveDrag->GrabFraction * FVector2D(Ext) : FVector2D::ZeroVector;
		R.ToLoot = Side != 0;
		R.Slot = View->Grid.Section;
		R.Cell = FIntPoint(FMath::FloorToInt(InGrid.X - Offset.X), FMath::FloorToInt(InGrid.Y - Offset.Y));
		// 用鼠标落点查找同分区内的其他物品作为合并候选；能否合并仍由请求处理逻辑判定。
		for (const auto& Other : Target->Items)
			if (Other.Id.IsValid() && Other.Id != Selected && Other.Slot == View->Grid.Section && InGrid.X >= Other.Cell.X
				&& InGrid.Y >= Other.Cell.Y && InGrid.X < Other.Cell.X + Other.Extent().X
				&& InGrid.Y < Other.Cell.Y + Other.Extent().Y)
			{
				R.MergeInto = Other.Id;
				break;
			}
		Canvas = Panel->ItemCanvas;
		At = FVector2D(R.Cell.X, View->Top + R.Cell.Y) * CellSize;
		Size = FVector2D(Ext) * CellSize - FVector2D(2);
		return true;
	}
	return false;
}

void ULootingMainView::HidePreview()
{
	if (DropPreview) DropPreview->SetVisibility(ESlateVisibility::Collapsed);
}

void ULootingMainView::DragOver(FVector2D Screen)
{
	UpdateDragRotation();
	FExtractionItemRequest R;
	UCanvasPanel* Canvas = nullptr;
	FVector2D At, Size;
	if ((!ActiveDrag && !bPendingPlacement) || !WidgetTree || !ResolveDrop(Screen, R, Canvas, At, Size))
	{
		HidePreview();
		return;
	}
	// 落点预览：可放下为绿色，可合并为琥珀色，放不下为红色。只用本地快照判断，最终结果以服务器为准。
	const auto* Source = Container(SelectedLoot);
	const auto* Item = Source ? Source->Find(Selected) : nullptr;
	const auto* Target = Container(R.ToLoot);
	bool bValid = false;
	if (Item && Target && !R.MergeInto.IsValid())
	{
		FExtractionContainer Copy = *Target;
		// 整组移动时原位置会空出来；拆分时原物品仍在，不能扣掉。
		if (R.ToLoot == SelectedLoot && R.Quantity >= Item->Quantity)
			Copy.Items.RemoveAll([this](const FExtractionItem& X) { return X.Id == Selected; });
		FExtractionItem Moved = *Item;
		Moved.Slot = R.Slot;
		Moved.Cell = R.Cell;
		Moved.Rotated = R.Rotated;
		bValid = ExtractionItems::Fits(Copy, Moved);
	}
	const FLinearColor Color = R.MergeInto.IsValid()
		                           ? ExtractionUIStyle::Accent()
		                           : bValid
		                           ? ExtractionUIStyle::Hex(TEXT("5DBB72"))
		                           : ExtractionUIStyle::Hex(TEXT("EA5A50"));
	if (!DropPreview)
	{
		DropPreview = WidgetTree->ConstructWidget<UBorder>();
		DropPreview->SetBrush(ExtractionUIStyle::Panel(FLinearColor::White, 3.f, FLinearColor::White, 2.f));
	}
	DropPreview->SetBrushColor(Color.CopyWithNewOpacity(.3f));
	if (DropPreview->GetParent() != Canvas)
	{
		DropPreview->RemoveFromParent();
		Canvas->AddChildToCanvas(DropPreview)->SetZOrder(100);
	}
	if (auto* S = Cast<UCanvasPanelSlot>(DropPreview->Slot))
	{
		S->SetPosition(At);
		S->SetSize(Size);
	}
	DropPreview->SetVisibility(ESlateVisibility::HitTestInvisible);
}

// 处理拖拽松手或拆分后的点击放置；返回 true 表示事件已处理，不代表物品移动成功。
bool ULootingMainView::PlaceAt(FVector2D Screen, UDragDropOperation* Operation)
{
	// 仅接收当前物品拖拽；没有拖拽且没有待放置物品时，不处理此次事件。
	if (Operation && (Operation != ActiveDrag || !Cast<ULootingDragDropOperation>(Operation))) return false;
	if (!ActiveDrag && !bPendingPlacement) return false;
	// 背包关闭、交互不可用、源物品已消失或未命中目标时只结束本次放置。
	const auto* I = Interaction();
	FExtractionItemRequest R;
	UCanvasPanel* Canvas = nullptr;
	FVector2D At, Size;
	if (I && I->InventoryOpen && I->CanAct() && ResolveDrop(Screen, R, Canvas, At, Size)) PC()->RequestItem(R);
	// 实际结果由请求处理逻辑决定。
	CancelDrag();
	return true;
}

FReply ULootingMainView::NativeOnPreviewKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	if (E.GetKey() == EKeys::Tab || E.GetKey() == EKeys::Escape || (E.GetKey() == EKeys::Enter && SplitDialog &&
		SplitDialog->IsVisible()))
		return NativeOnKeyDown(G, E);
	return Super::NativeOnPreviewKeyDown(G, E);
}

FReply ULootingMainView::NativeOnKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	if (!PC()) return FReply::Unhandled();
	if (E.GetKey() == EKeys::Tab)
	{
		CancelDrag();
		PC()->ToggleInventory();
		return FReply::Handled();
	}
	if (E.GetKey() == EKeys::Escape)
	{
		PC()->ToggleExtractionMenu();
		return FReply::Handled();
	}
	if (E.GetKey() == EKeys::Enter && SplitDialog && SplitDialog->IsVisible())
	{
		MenuAction(-2);
		return FReply::Handled();
	}
	if (E.GetKey() == EKeys::R && !E.IsRepeat())
	{
		if (!bRotationDown) RotateDrag();
		bRotationDown = true;
		return FReply::Handled();
	}
	if (E.GetKey() == EKeys::F && !E.IsRepeat())
	{
		TakeAllLoot();
		return FReply::Handled();
	}
	if (E.GetKey() == EKeys::T && !E.IsRepeat())
	{
		SortOwn();
		return FReply::Handled();
	}
	if (E.GetKey() == EKeys::H)
	{
		PC()->UseMedical();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(G, E);
}

FReply ULootingMainView::NativeOnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	if (bPendingPlacement && E.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		PlaceAt(E.GetScreenSpacePosition());
		return FReply::Handled();
	}
	if (ContextMenu && ContextMenu->IsVisible())
	{
		CancelDrag();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(G, E);
}

bool ULootingMainView::NativeOnDrop(const FGeometry&, const FDragDropEvent& E, UDragDropOperation* Op)
{
	return PlaceAt(E.GetScreenSpacePosition(), Op);
}

bool ULootingMainView::NativeOnDragOver(const FGeometry&, const FDragDropEvent& E, UDragDropOperation*)
{
	DragOver(E.GetScreenSpacePosition());
	return ActiveDrag != nullptr;
}
