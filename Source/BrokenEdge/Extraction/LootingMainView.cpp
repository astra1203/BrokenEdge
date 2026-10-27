#include "LootingMainView.h"
#include "ExtractionPlayerController.h"
#include "ExtractionInteractionComponent.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionLootActor.h"
#include "ExtractionKeyRoom.h"
#include "ExtractionState.h"
#include "ExtractionGunsmith.h"
#include "ExtractionUIStyle.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
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
}

void ULootingWidgetPart::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	if (ActionButton) ActionButton->OnClicked.AddDynamic(this, &ThisClass::ClickAction);
}

void ULootingWidgetPart::ClickAction() { if (Inventory) Inventory->MenuAction(ActionIndex); }

void ULootingWidgetPart::SetItem(const FExtractionItem& Item)
{
	ItemId = Item.Id;
	const auto* D = UExtractionItemDefinition::Get(Item.Definition);
	Text(Label, D ? D->Label.ToString() : TEXT("未搜索"));
	Text(Detail, D && D->Kind == EExtractionItemKind::Weapon
		             ? FString::Printf(TEXT("%d / %d"), Item.Magazine, D->MagazineCapacity)
		             : FString::Printf(TEXT("×%d"), Item.Quantity));
	if (Icon)
	{
		auto* Texture = D && !D->Icon.IsNull() ? D->Icon.LoadSynchronous() : nullptr;
		Icon->SetBrushFromTexture(Texture, true);
		Icon->SetVisibility(Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (RarityBar) RarityBar->SetBrushColor(ExtractionUIStyle::Rarity(D ? D->DisplayRarity : 0));
	if (Item.Id.IsValid())
	{
		const auto Lines = ExtractionItems::WeaponDetails(Item);
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

void ULootingWidgetPart::NativeOnDragDetected(const FGeometry&, const FPointerEvent&, UDragDropOperation*& Out)
{
	if (Inventory && ItemId.IsValid()) Inventory->BeginDrag(this, Out);
}

bool ULootingWidgetPart::NativeOnDrop(const FGeometry&, const FDragDropEvent& E, UDragDropOperation* Op)
{
	return Inventory && Inventory->PlaceAt(E.GetScreenSpacePosition(), Op);
}

bool ULootingWidgetPart::NativeOnDragOver(const FGeometry&, const FDragDropEvent&, UDragDropOperation*)
{
	if (Inventory) Inventory->UpdateDragRotation();
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
	SetIsFocusable(true);
	for (auto* Part : {
		     OwnContainer.Get(), LootContainer.Get(), ContextMenu.Get(), SplitDialog.Get(), InteractionPrompt.Get()
	     })
		if (Part) Part->Inventory = this;
	if (SplitDialog) SplitDialog->ActionIndex = -2;
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
	const TCHAR* Names[] = {TEXT("手持武器"),TEXT("背负武器"),TEXT("头盔"),TEXT("胸甲")};
	for (int32 N = 0; N < 4; ++N)
	{
		auto* EquipmentView = MakePart(SlotClass);
		if (!EquipmentView) continue;
		EquipmentView->bLoot = Loot;
		EquipmentView->EquipmentSlot = N;
		Text(EquipmentView->Label, Names[N]);
		List->AddChildToVerticalBox(EquipmentView)->SetPadding(FMargin(0, 0, 0, 12));
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
	const FVector2D Size(C->Width * CellSize, C->Height * CellSize);
	if (Panel->ContentSize)
	{
		Panel->ContentSize->SetWidthOverride(Size.X);
		Panel->ContentSize->SetHeightOverride(Size.Y);
	}
	for (int32 Y = 0; Y < C->Height; ++Y)
		for (int32 X = 0; X < C->Width; ++X)
			if (auto* Cell = MakePart(CellClass))
			{
				Cell->SetVisibility(ESlateVisibility::HitTestInvisible);
				CanvasChild(Panel->GridCanvas, Cell, FVector2D(X, Y) * CellSize, FVector2D(CellSize - 2));
			}
	for (const auto& Item : C->Items)
	{
		if (Item.Slot >= 0) continue;
		if (auto* Card = MakePart(ItemClass))
		{
			Card->bLoot = Loot;
			Card->SetItem(Item);
			CanvasChild(Panel->ItemCanvas, Card, FVector2D(Item.Cell) * CellSize,
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
		if (Own && (!bWasOpen || Own->Id != LastOwnId || Own->Revision != LastOwnRevision))
		{
			RebuildEquipment(EquipmentList, false);
			RebuildContainer(OwnContainer, false);
			LastOwnId = Own->Id;
			LastOwnRevision = Own->Revision;
		}
		if (!bWasOpen || (Loot ? Loot->Id : FGuid()) != LastLootId || (Loot ? Loot->Revision : INDEX_NONE) !=
			LastLootRevision)
		{
			RebuildContainer(LootContainer, true);
			LastLootId = Loot ? Loot->Id : FGuid();
			LastLootRevision = Loot ? Loot->Revision : INDEX_NONE;
		}
		Text(OwnTitle, Own ? FString::Printf(TEXT("背包  /  %d × %d"), Own->Width, Own->Height) : TEXT("背包"));
		const auto* GS = GetWorld()->GetGameState<AExtractionGameState>();
		const double Now = GS ? GS->GetServerWorldTimeSeconds() : 0;
		Text(LootTitle, I->Loot
			                ? I->Loot->Label + (I->SearchEnd > Now
				                                    ? FString::Printf(TEXT("  · 搜索中 %.1fs"), I->SearchEnd - Now)
				                                    : TEXT(""))
			                : TEXT("搜索容器以查看物资"));
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
	if (I->DoorFocus)
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
		Prompt = TEXT("E  ") + Label + (I->Focus->Loose ? TEXT(" · 拾取") : TEXT(" · 搜索"));
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
	const FVector2D Limit = G.GetLocalSize() - FVector2D(300, Labels.Num() * 42 + 24);
	P.X = FMath::Clamp(P.X, 0., FMath::Max(0., Limit.X));
	P.Y = FMath::Clamp(P.Y, 0., FMath::Max(0., Limit.Y));
	if (auto* S = Cast<UCanvasPanelSlot>(ContextMenu->Slot)) S->SetPosition(P);
	ContextMenu->SetVisibility(ESlateVisibility::Visible);
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
	if (!D || D->Kind != EExtractionItemKind::Weapon)
	{
		PC()->ShowItemMessage(TEXT("只有武器可以装备"));
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
	Op->Pivot = EDragPivot::MouseDown;
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

bool ULootingMainView::PlaceAt(FVector2D Screen, UDragDropOperation* Operation)
{
	if (Operation && (Operation != ActiveDrag || !Cast<ULootingDragDropOperation>(Operation))) return false;
	if (!ActiveDrag && !bPendingPlacement) return false;
	auto* I = Interaction();
	const auto* C = Container(SelectedLoot);
	const auto* Item = C ? C->Find(Selected) : nullptr;
	if (!I || !I->InventoryOpen || !I->CanAct() || !Item)
	{
		CancelDrag();
		return true;
	}
	auto R = Request();
	R.AutoPlace = false;
	for (const auto& Target : EquipmentTargets)
		if (Contains(Target, Screen))
		{
			R.ToLoot = Target->bLoot;
			R.Slot = Target->EquipmentSlot;
			PC()->RequestItem(R);
			CancelDrag();
			return true;
		}
	for (int32 Side = 0; Side < 2; ++Side)
	{
		auto* Panel = Side ? LootContainer.Get() : OwnContainer.Get();
		const auto* Target = Container(Side != 0);
		if (!Panel || !Target || !Contains(Panel->ItemCanvas, Screen)) continue;
		const FVector2D Local = Panel->ItemCanvas->GetCachedGeometry().AbsoluteToLocal(Screen) / CellSize;
		const FIntPoint Ext = Rotated ? FIntPoint(Item->Size.Y, Item->Size.X) : Item->Size;
		const FVector2D Offset = ActiveDrag ? ActiveDrag->GrabFraction * FVector2D(Ext) : FVector2D::ZeroVector;
		R.ToLoot = Side != 0;
		R.Cell = FIntPoint(FMath::FloorToInt(Local.X - Offset.X), FMath::FloorToInt(Local.Y - Offset.Y));
		for (const auto& Other : Target->Items)
			if (Other.Id.IsValid() && Other.Id != Selected && Other.Slot < 0 && Local.X >= Other.Cell.X && Local.Y >=
				Other.Cell.Y && Local.X < Other.Cell.X + Other.Extent().X && Local.Y < Other.Cell.Y + Other.Extent().Y)
			{
				R.MergeInto = Other.Id;
				break;
			}
		PC()->RequestItem(R);
		CancelDrag();
		return true;
	}
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

bool ULootingMainView::NativeOnDragOver(const FGeometry&, const FDragDropEvent&, UDragDropOperation*)
{
	UpdateDragRotation();
	return ActiveDrag != nullptr;
}
