#include "ExtractionInventoryWidget.h"
#include "ExtractionPlayerController.h"
#include "ExtractionInteractionComponent.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionLootActor.h"
#include "ExtractionState.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Input/Reply.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
	constexpr float CellSize = 58;
	FVector2D GridOrigin(bool Loot) { return FVector2D(Loot ? 670 : 140, 240); }
	FVector2D SlotOrigin(bool Loot, int32 Slot) { return FVector2D((Loot ? 670 : 140) + (Slot % 2) * 182, Slot < 2 ? 165 : 490); }

	bool Inside(FVector2D P, FVector2D Origin, FVector2D Size)
	{
		return P.X >= Origin.X && P.Y >= Origin.Y && P.X < Origin.X + Size.X && P.Y < Origin.Y + Size.Y;
	}

	FVector2D ItemOrigin(const FExtractionItem& I, bool Loot)
	{
		return I.Slot >= 0 ? SlotOrigin(Loot, I.Slot) : GridOrigin(Loot) + FVector2D(I.Cell.X, I.Cell.Y) * CellSize;
	}

	FVector2D ItemSize(const FExtractionItem& I)
	{
		return I.Slot >= 0 ? FVector2D(166, 56) : FVector2D(I.Extent().X, I.Extent().Y) * CellSize;
	}
}

class SExtractionInventoryPanel : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SExtractionInventoryPanel)
		{
		}

		SLATE_ARGUMENT(UExtractionInventoryWidget*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args) { Owner = Args._Owner; }
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1280, 720); }

	AExtractionPlayerController* PC() const
	{
		return Owner.IsValid() ? Cast<AExtractionPlayerController>(Owner->GetOwningPlayer()) : nullptr;
	}

	UExtractionInteractionComponent* Interaction() const { return PC() ? PC()->Interaction() : nullptr; }

	const FExtractionContainer* Container(bool Loot) const
	{
		auto* I = Interaction();
		return !I || !I->Inventory() ? nullptr : Loot ? I->Loot ? &I->LootView : nullptr : &I->Inventory()->Contents;
	}

	bool Cancel()
	{
		const bool WasBusy = Dragging || MenuOpen || Splitting;
		Dragging = MenuOpen = Splitting = false;
		Selected.Invalidate();
		return WasBusy;
	}

	void Refresh()
	{
		if (!Interaction() || !Interaction()->InventoryOpen) Cancel();
		if (Selected.IsValid())
		{
			const auto* C = Container(SelectedLoot);
			if (!C || !C->Find(Selected)) Cancel();
		}
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	                      FSlateWindowElementList& Draw, int32 Layer, const FWidgetStyle& Style,
	                      bool Enabled) const override
	{
		auto* I = Interaction();
		if (!I || !I->CanAct()) return Layer;
		const float Scale = FMath::Min(Geometry.GetLocalSize().X / 1280, Geometry.GetLocalSize().Y / 720);
		auto Box = [&](FVector2D P, FVector2D Size, FLinearColor Color, int32 Z = 0)
		{
			FSlateDrawElement::MakeBox(Draw, Layer + Z,
			                           Geometry.ToPaintGeometry(Size * Scale, FSlateLayoutTransform(P * Scale)),
			                           FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, Color);
		};
		auto Text = [&](FVector2D P, const FString& Label, int32 FontSize = 15,
		                FLinearColor Color = FLinearColor::White, int32 Z = 1)
		{
			FSlateDrawElement::MakeText(Draw, Layer + Z,
			                            Geometry.ToPaintGeometry(FVector2D(1100, 60) * Scale,
			                                                     FSlateLayoutTransform(P * Scale)),
			                            Label, FCoreStyle::GetDefaultFontStyle(
				                            "Regular", FMath::Max(8, FMath::RoundToInt(FontSize * Scale))),
			                            ESlateDrawEffect::None, Color);
		};
		const auto* GS = GetWorldState();
		const double Now = GS ? GS->GetServerWorldTimeSeconds() : 0;
		if (!I->InventoryOpen)
		{
			if (I->Focus)
			{
				FString Label = I->Focus->Label;
				if (I->Focus->Loose)
					if (const auto* D = UExtractionItemDefinition::Get(I->Focus->LooseDefinition)) Label = D->Label.
						ToString();
				Box(FVector2D(450, 505), FVector2D(420, 44), FLinearColor(.015f, .025f, .035f, .9f));
				Text(FVector2D(465, 518), TEXT("E  ") + Label + (I->Focus->Loose ? TEXT(" · 拾取") : TEXT(" · 搜索")), 17);
			}
			Text(FVector2D(425, 680), TEXT("Tab 背包    H 医疗    E 交互 / 切枪"), 14, FLinearColor(.7f, .8f, .85f));
			if (I->MedicalEnd > 0)
			{
				const float Remaining = FMath::Max(0.0, I->MedicalEnd - Now);
				Box(FVector2D(450, 565), FVector2D(380, 7), FLinearColor(.1f, .15f, .18f));
				Box(FVector2D(450, 565), FVector2D(380 * FMath::Clamp(1 - Remaining / I->MedicalDuration, 0.0, 1.0), 7),
				    FLinearColor(.15f, .9f, .6f));
				Text(FVector2D(450, 580), FString::Printf(TEXT("治疗中 %.1f 秒 · H / Esc 取消"), Remaining));
			}
			if (!PC()->ItemMessage.IsEmpty()) Text(FVector2D(420, 635), PC()->ItemMessage, 15,
			                                       FLinearColor(1, .65f, .3f));
			return Layer + 2;
		}
		Box(FVector2D(60, 75), FVector2D(1160, 570), FLinearColor(.018f, .028f, .042f, .98f));
		Box(FVector2D(60, 75), FVector2D(5, 570), FLinearColor(.12f, .8f, .72f));
		Text(FVector2D(95, 95), TEXT("战地物资"), 27);
		Text(FVector2D(95, 135), TEXT("背包 · 6 × 4"), 17, FLinearColor(.55f, .85f, .8f));
		Text(FVector2D(670, 135), I->Loot ? I->Loot->Label : TEXT("搜索容器以查看物资"), 17, FLinearColor(.55f, .85f, .8f));
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const bool Loot = Side != 0;
			const auto* C = Container(Loot);
			if (!C) continue;
			if (C->WeaponSlots)
				for (int32 Slot = 0; Slot < 4; ++Slot)
				{
					Box(SlotOrigin(Loot, Slot), FVector2D(166, 56), FLinearColor(.08f, .13f, .16f));
					if (!C->InSlot(Slot)) Text(SlotOrigin(Loot, Slot) + FVector2D(8, 18),
					                           Slot == 0 ? TEXT("手持武器") : Slot == 1 ? TEXT("背负武器") : Slot == 2 ? TEXT("头盔") : TEXT("胸甲"), 14,
					                           FLinearColor(.45f, .55f, .6f));
				}
			for (int32 Y = 0; Y < C->Height; ++Y)
				for (int32 X = 0; X < C->Width; ++X)
					Box(GridOrigin(Loot) + FVector2D(X, Y) * CellSize, FVector2D(CellSize - 2, CellSize - 2),
					    FLinearColor(.07f, .105f, .135f));
			for (const auto& Item : C->Items)
			{
				const FVector2D P = ItemOrigin(Item, Loot) + FVector2D(2, 2);
				const auto* D = UExtractionItemDefinition::Get(Item.Definition);
				const FLinearColor Color = !D
					                           ? FLinearColor(.14f, .15f, .19f)
					                           : D->Kind == EExtractionItemKind::Medical
					                           ? FLinearColor(.08f, .29f, .22f)
					                           : D->Kind == EExtractionItemKind::Weapon
					                           ? FLinearColor(.23f, .22f, .16f)
					                           : FLinearColor(.13f, .23f, .29f);
				Box(P, ItemSize(Item) - FVector2D(5, 5), Color);
				FString Label = D ? D->Label.ToString() : TEXT("?");
				Text(P + FVector2D(5, 5), Label, Item.Extent().X == 1 && Item.Slot < 0 ? 9 : 14);
				if (D)
					Text(P + FVector2D(5, 29), ExtractionItems::Details(Item), 12,
					     FLinearColor(.8f, .9f, .95f));
			}
		}
		if (I->SearchEnd > 0)
			Text(FVector2D(670, 552), FString::Printf(TEXT("搜索中… %.1f 秒"), FMath::Max(0.0, I->SearchEnd - Now)), 16,
			     FLinearColor(.2f, .9f, .75f));
		Text(FVector2D(100, 548), PC()->ItemMessage, 16, FLinearColor(1, .65f, .3f));
		Text(FVector2D(100, 589), TEXT("拖动放置 · R 旋转 · Shift 点击快捷转移 · 右键操作"), 15, FLinearColor(.68f, .78f, .82f));
		Text(FVector2D(100, 613), TEXT("Tab / Esc 关闭 · 占用的武器槽需先移出原武器"), 14, FLinearColor(.48f, .6f, .68f));
		if (Dragging)
		{
			if (const auto* C = Container(SelectedLoot))
				if (const auto* Item = C->Find(Selected))
				{
					FExtractionItem Ghost = *Item;
					Ghost.Rotated = Rotated;
					Ghost.Slot = -1;
					Box(Mouse + FVector2D(8, 8), ItemSize(Ghost) - FVector2D(4, 4), FLinearColor(.2f, .6f, .65f, .65f),
					    3);
					Text(Mouse + FVector2D(15, 15), FString::Printf(TEXT("× %d  · R 旋转"), Quantity), 14,
					     FLinearColor::White, 4);
				}
		}
		if (MenuOpen)
		{
			Box(MenuPosition, FVector2D(250, MenuHeight()), FLinearColor(.045f, .065f, .09f), 5);
			const TCHAR* Labels[] = {TEXT("快捷转移"), TEXT("装备 / 卸下"), TEXT("使用"), TEXT("拆分数量…"), TEXT("丢弃")};
			for (int32 Row = 0; Row < 5; ++Row) Text(MenuPosition + FVector2D(12, 10 + Row * 32), Labels[Row], 15,
			                                         FLinearColor::White, 6);
			const auto Actions = EquipmentActions();
			for (int32 N = 0; N < Actions.Num(); ++N) Text(MenuPosition + FVector2D(12, 10 + (N + 5) * 32), Actions[N].Label, 14, FLinearColor::White, 6);
		}
		if (Splitting)
		{
			Box(FVector2D(430, 255), FVector2D(420, 165), FLinearColor(.055f, .085f, .11f), 7);
			Text(FVector2D(450, 275), TEXT("拆分数量"), 20, FLinearColor::White, 8);
			Text(FVector2D(450, 315), SplitText + TEXT("  （输入数字，↑↓调整）"), 18, FLinearColor(.3f, 1, .8f), 8);
			Text(FVector2D(450, 368), TEXT("Enter 开始拖放    Esc 取消"), 16, FLinearColor::White, 8);
		}
		return Layer + 9;
	}

	virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override
	{
		Mouse = Position(G, E);
		Invalidate(EInvalidateWidgetReason::Paint);
		return Interaction() && Interaction()->InventoryOpen ? FReply::Handled() : FReply::Unhandled();
	}

	virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override
	{
		if (!Interaction() || !Interaction()->InventoryOpen) return FReply::Unhandled();
		Mouse = Position(G, E);
		if (Splitting) return FReply::Handled().SetUserFocus(AsShared());
		if (MenuOpen && E.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			const int32 Row = FMath::FloorToInt((Mouse.Y - MenuPosition.Y - 5) / 32);
			MenuOpen = false;
			if (Inside(Mouse, MenuPosition, FVector2D(250, MenuHeight())) && Row >= 0 && Row < 5 + EquipmentActions().Num()) MenuAction(Row);
			return FReply::Handled().SetUserFocus(AsShared());
		}
		if (Dragging)
		{
			DropAt(Mouse);
			return FReply::Handled().ReleaseMouseCapture();
		}
		bool Loot = false;
		const auto* Item = HitItem(Mouse, Loot);
		if (!Item || !Item->Id.IsValid())
		{
			MenuOpen = false;
			return FReply::Handled().SetUserFocus(AsShared());
		}
		Selected = Item->Id;
		SelectedLoot = Loot;
		Quantity = Item->Quantity;
		Rotated = Item->Rotated;
		if (E.GetEffectingButton() == EKeys::RightMouseButton)
		{
			MenuPosition = FVector2D(FMath::Min(Mouse.X, 960.f), FMath::Min(Mouse.Y, 690.f - MenuHeight()));
			MenuOpen = true;
			return FReply::Handled().SetUserFocus(AsShared());
		}
		if (E.IsShiftDown())
		{
			QuickTransfer();
			return FReply::Handled().SetUserFocus(AsShared());
		}
		Dragging = true;
		return FReply::Handled().CaptureMouse(AsShared()).SetUserFocus(AsShared());
	}

	virtual FReply OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E) override
	{
		if (Dragging && E.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			DropAt(Position(G, E));
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& E) override
	{
		if (!PC()) return FReply::Unhandled();
		const FKey Key = E.GetKey();
		if (Key == EKeys::Escape)
		{
			PC()->ToggleExtractionMenu();
			return FReply::Handled().ReleaseMouseCapture();
		}
		if (Key == EKeys::Tab)
		{
			Cancel();
			PC()->ToggleInventory();
			return FReply::Handled().ReleaseMouseCapture();
		}
		if (Splitting)
		{
			const auto* Item = Container(SelectedLoot) ? Container(SelectedLoot)->Find(Selected) : nullptr;
			if (!Item)
			{
				Cancel();
				return FReply::Handled();
			}
			if (Key == EKeys::Up || Key == EKeys::Down)
				SplitText = FString::FromInt(FMath::Clamp(FCString::Atoi(*SplitText) + (Key == EKeys::Up ? 1 : -1), 1,
				                                          Item->Quantity - 1));
			if (Key == EKeys::BackSpace) SplitText.LeftChopInline(1);
			if (Key == EKeys::Enter)
			{
				Quantity = FMath::Clamp(FCString::Atoi(*SplitText), 1, Item->Quantity - 1);
				Splitting = false;
				Dragging = true;
			}
			return FReply::Handled();
		}
		if (Key == EKeys::R)
		{
			if (Dragging) Rotated = !Rotated;
			return FReply::Handled();
		}
		if (Key == EKeys::H)
		{
			PC()->UseMedical();
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnKeyChar(const FGeometry&, const FCharacterEvent& E) override
	{
		if (Splitting && E.GetCharacter() >= '0' && E.GetCharacter() <= '9')
		{
			if (SplitText.Len() >= 3) SplitText.Reset();
			SplitText.AppendChar(E.GetCharacter());
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

private:
	TArray<ExtractionItems::FEquipmentAction> EquipmentActions() const
	{
		const auto* C = Container(false);
		return SelectedLoot || !C ? TArray<ExtractionItems::FEquipmentAction>() : ExtractionItems::EquipmentActions(*C, Selected);
	}
	float MenuHeight() const { return 14.f + 32.f * (5 + EquipmentActions().Num()); }
	TWeakObjectPtr<UExtractionInventoryWidget> Owner;
	FGuid Selected;
	bool SelectedLoot = false, Dragging = false, Rotated = false, MenuOpen = false, Splitting = false;
	int32 Quantity = 1;
	FString SplitText;
	FVector2D Mouse = FVector2D::ZeroVector, MenuPosition = FVector2D::ZeroVector;

	const AExtractionGameState* GetWorldState() const
	{
		return PC() ? PC()->GetWorld()->GetGameState<AExtractionGameState>() : nullptr;
	}

	FVector2D Position(const FGeometry& G, const FPointerEvent& E) const
	{
		return G.AbsoluteToLocal(E.GetScreenSpacePosition()) / FMath::Max(
			.1f, FMath::Min(G.GetLocalSize().X / 1280, G.GetLocalSize().Y / 720));
	}

	const FExtractionItem* HitItem(FVector2D P, bool& Loot) const
	{
		for (int32 Side = 0; Side < 2; ++Side)
			if (const auto* C = Container(Side != 0))
				for (const auto& Item : C->Items)
					if (Inside(P, ItemOrigin(Item, Side != 0), ItemSize(Item)))
					{
						Loot = Side != 0;
						return &Item;
					}
		return nullptr;
	}

	FExtractionItemRequest Request() const
	{
		FExtractionItemRequest R;
		R.Item = Selected;
		R.FromLoot = SelectedLoot;
		R.Quantity = Quantity;
		R.Rotated = Rotated;
		return R;
	}

	void QuickTransfer()
	{
		auto R = Request();
		R.ToLoot = !SelectedLoot && Interaction()->Loot != nullptr;
		if (!SelectedLoot && !R.ToLoot)
		{
			EquipOrStore();
			return;
		}
		PC()->RequestItem(R);
		Cancel();
	}

	void EquipOrStore()
	{
		const auto* C = Container(SelectedLoot);
		const auto* Item = C ? C->Find(Selected) : nullptr;
		if (!Item || Item->Definition != TEXT("AK"))
		{
			PC()->ItemMessage = TEXT("只有武器可以装备");
			Cancel();
			return;
		}
		auto R = Request();
		R.ToLoot = false;
		if (SelectedLoot || Item->Slot < 0)
		{
			const auto* Own = Container(false);
			R.Slot = !Own->InSlot(0) ? 0 : !Own->InSlot(1) ? 1 : -1;
			if (R.Slot < 0)
			{
				PC()->ItemMessage = TEXT("武器槽已满，请先移出原武器");
				Cancel();
				return;
			}
		}
		PC()->RequestItem(R);
		Cancel();
	}

	void MenuAction(int32 Row)
	{
		if (Row >= 5)
		{
			const auto Actions = EquipmentActions();
			if (Actions.IsValidIndex(Row - 5)) PC()->RequestItem(Actions[Row - 5].Request);
			Cancel(); return;
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
			if (Quantity <= 1)
			{
				PC()->ItemMessage = TEXT("该物品无法拆分");
				Cancel();
				return;
			}
			SplitText = FString::FromInt(Quantity / 2);
			Splitting = true;
			return;
		}
		auto R = Request();
		R.Op = Row == 2 ? EExtractionItemOp::Use : EExtractionItemOp::Drop;
		PC()->RequestItem(R);
		Cancel();
	}

	void DropAt(FVector2D P)
	{
		auto R = Request();
		R.AutoPlace = false;
		bool TargetLoot = false;
		const auto* TargetItem = HitItem(P, TargetLoot);
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const auto* C = Container(Side != 0);
			if (!C) continue;
			R.ToLoot = Side != 0;
			if (C->WeaponSlots)
				for (int32 Slot = 0; Slot < 4; ++Slot)
					if (Inside(P, SlotOrigin(Side != 0, Slot), FVector2D(166, 56)))
					{
						R.Slot = Slot;
						PC()->RequestItem(R);
						Cancel();
						return;
					}
			if (!Inside(P, GridOrigin(Side != 0), FVector2D(C->Width, C->Height) * CellSize)) continue;
			const FVector2D Cell = (P - GridOrigin(Side != 0)) / CellSize;
			R.Cell = FIntPoint(FMath::FloorToInt(Cell.X), FMath::FloorToInt(Cell.Y));
			if (TargetItem && TargetLoot == (Side != 0) && TargetItem->Id.IsValid() && TargetItem->Id != Selected)
				R.MergeInto = TargetItem->Id;
			PC()->RequestItem(R);
			Cancel();
			return;
		}
		Cancel();
	}
};

TSharedRef<SWidget> UExtractionInventoryWidget::RebuildWidget()
{
	return SAssignNew(Panel, SExtractionInventoryPanel).Owner(this);
}

void UExtractionInventoryWidget::ReleaseSlateResources(bool ReleaseChildren)
{
	Super::ReleaseSlateResources(ReleaseChildren);
	Panel.Reset();
}

void UExtractionInventoryWidget::Refresh()
{
	const auto* PC = Cast<AExtractionPlayerController>(GetOwningPlayer());
	const auto* I = PC ? PC->Interaction() : nullptr;
	SetVisibility(I && I->InventoryOpen ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
	if (Panel) Panel->Refresh();
}

bool UExtractionInventoryWidget::CancelDrag() { return Panel && Panel->Cancel(); }
