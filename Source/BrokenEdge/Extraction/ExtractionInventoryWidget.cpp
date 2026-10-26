#include "ExtractionInventoryWidget.h"
#include "ExtractionUIStyle.h"
#include "ExtractionGunsmith.h"
#include "ExtractionPlayerController.h"
#include "ExtractionInteractionComponent.h"
#include "ExtractionKeyRoom.h"
#include "ExtractionItems.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionLootActor.h"
#include "ExtractionState.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Input/Reply.h"
#include "Framework/Application/SlateApplication.h"
// 局内背包的 UMG 外壳与 Slate 交互面板：读取背包及搜索后可见的战利品快照，提交物品操作意图。
// 拖拽、旋转、菜单和拆分数量都是本地界面状态；真正的容器变更由服务器校验并复制回来。

namespace
{
	constexpr float CellSize = 58;
	FVector2D GridOrigin(bool Loot) { return FVector2D(Loot ? 670 : 140, 240); }

	FVector2D SlotOrigin(bool Loot, int32 Slot)
	{
		return FVector2D((Loot ? 670 : 140) + (Slot % 2) * 182, Slot < 2 ? 165 : 490);
	}

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

	// 按统一缩放绘制背包、已搜索战利品、拖拽预览及菜单；未知物品不提供可操作 ID。
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	                      FSlateWindowElementList& Draw, int32 Layer, const FWidgetStyle& Style,
	                      bool Enabled) const override
	{
		auto* I = Interaction();
		if (!I || !I->CanAct()) return Layer;
		const float Scale = ExtractionUIStyle::Scale(Geometry);
        const FVector2D CanvasOffset = ExtractionUIStyle::Offset(Geometry);
		auto Box = [&](FVector2D P, FVector2D Size, FLinearColor Color, int32 Z = 0)
		{
			FSlateDrawElement::MakeBox(Draw, Layer + Z,
			                           Geometry.ToPaintGeometry(Size * Scale, FSlateLayoutTransform(CanvasOffset + P * Scale)),
			                           FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, Color);
		};
		auto Text = [&](FVector2D P, const FString& Label, int32 FontSize = 15,
		                FLinearColor Color = FLinearColor::White, int32 Z = 1)
		{
			FSlateDrawElement::MakeText(Draw, Layer + Z,
			                            Geometry.ToPaintGeometry(FVector2D(1100, 60) * Scale,
			                                                     FSlateLayoutTransform(CanvasOffset + P * Scale)),
			                            Label, FCoreStyle::GetDefaultFontStyle(
				                            "Regular", FMath::Max(8, FMath::RoundToInt(FontSize * Scale))),
			                            ESlateDrawEffect::None, Color);
		};
		const auto* GS = GetWorldState();
		const double Now = GS ? GS->GetServerWorldTimeSeconds() : 0;
		if (!I->InventoryOpen)
		{
			if (I->DoorFocus)
			{
				// 只有站进门的交互碰撞盒才会走到这里；再按"手上有没有这张卡"切换文案。
				const FString Room = I->DoorFocus->RoomLabel.IsEmpty()
					                     ? FString(TEXT("上锁房间"))
					                     : I->DoorFocus->RoomLabel.ToString();
				const FText Card = I->DoorFocus->RequiredCardLabel.IsEmpty()
					                   ? FText::FromString(TEXT("对应钥匙卡"))
					                   : I->DoorFocus->RequiredCardLabel;
				int32 Cards = 0;
				int32 Uses = 0;
				if (const auto* Inventory = I->Inventory())
					for (const auto& Item : Inventory->Contents.Items)
					{
						const auto* D = UExtractionItemDefinition::Get(Item.Definition);
						if (!D) continue;
						const bool bMatch = I->DoorFocus->RequiredKey.IsNone()
							                    ? Item.Definition == TEXT("Key")
							                    : D->KeyRoomId == I->DoorFocus->RequiredKey;
						if (!bMatch) continue;
						++Cards;
						Uses += FMath::Max(1, FMath::RoundToInt(Item.Durability));
					}
				const FString Prompt = Cards > 0
					                       ? FString::Printf(TEXT("F  %s · 使用%s 开门   ·   剩余 %d 次"),
					                                         *Room, *Card.ToString(), Uses)
					                       : FString::Printf(TEXT("%s · 需要%s   ·   未持有"),
					                                         *Room, *Card.ToString());
				const FLinearColor Tint = Cards > 0
					                          ? FLinearColor::White
					                          : FLinearColor(.72f, .56f, .32f);
				Box(FVector2D(360, 505), FVector2D(620, 44), FLinearColor(.015f, .025f, .035f, .9f));
				Text(FVector2D(375, 518), Prompt, 17, Tint);
			}
			if (I->Focus)
			{
				FString Label = I->Focus->Label;
				if (I->Focus->Loose)
					if (const auto* D = UExtractionItemDefinition::Get(I->Focus->LooseDefinition))
						Label = D->Label.
						           ToString();
				Box(FVector2D(450, 505), FVector2D(420, 44), FLinearColor(.015f, .025f, .035f, .9f));
				Text(FVector2D(465, 518), TEXT("E  ") + Label + (I->Focus->Loose ? TEXT(" · 拾取") : TEXT(" · 搜索")), 17);
			}
			Text(FVector2D(320, 680), TEXT("Tab 背包    H 医疗    E 交互    1 当前枪    2 切枪    3 战术刀"), 14,
			     FLinearColor(.7f, .8f, .85f));
			if (I->MedicalEnd > 0)
			{
				const float Remaining = FMath::Max(0.0, I->MedicalEnd - Now);
				Box(FVector2D(450, 565), FVector2D(380, 7), FLinearColor(.1f, .15f, .18f));
				Box(FVector2D(450, 565), FVector2D(380 * FMath::Clamp(1 - Remaining / I->MedicalDuration, 0.0, 1.0), 7),
				    FLinearColor(.15f, .9f, .6f));
				Text(FVector2D(450, 580), FString::Printf(TEXT("治疗中 %.1f 秒 · H / Esc 取消"), Remaining));
			}
			if (!PC()->ItemMessage.IsEmpty())
				Text(FVector2D(420, 635), PC()->ItemMessage, 15,
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
					if (!C->InSlot(Slot))
						Text(SlotOrigin(Loot, Slot) + FVector2D(8, 18),
						     Slot == 0 ? TEXT("手持武器") : Slot == 1 ? TEXT("背负武器") : Slot == 2 ? TEXT("头盔") : TEXT("胸甲"),
						     14,
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
                Box(P, FVector2D(ItemSize(Item).X-5,2),ExtractionUIStyle::Rarity(D ? D->DisplayRarity : 0),1);
                const FVector2D Bounds=ItemSize(Item)-FVector2D(5,5);
                const bool HasIcon=ExtractionUIStyle::Icon(Draw,Layer+1,Geometry,P+FVector2D(4,4),Bounds-FVector2D(8,20),Scale,D);
                const FString Label=D ? D->Label.ToString() : TEXT("?");
                if (!HasIcon) Text(P+FVector2D(5,5),Label.Left(Item.Extent().X==1 && Item.Slot<0 ? 4 : 12),Item.Slot>=0 ? 12 : 10);
                const FString Detail=Item.Slot<0 ? FString::Printf(TEXT("×%d"),Item.Quantity)
                    : D && D->Kind==EExtractionItemKind::Weapon ? FString::Printf(TEXT("%s  %d/%d"),*Label,Item.Magazine,D->MagazineCapacity)
                    : Label;
                Text(P+FVector2D(4,Bounds.Y-16),Detail,Item.Slot>=0 ? 11 : 10,FLinearColor(.85f,.92f,.92f),2);
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
		if (MenuOpen && !Dragging)
			if (const auto* C = Container(SelectedLoot))
				if (const auto* Item = C->Find(Selected))
				{
					const auto Lines = ExtractionItems::WeaponDetails(*Item);
					if (!Lines.IsEmpty())
					{
						const FVector2D At(MenuPosition.X > 640 ? 100 : 740, 80);
						Box(At, FVector2D(440, 22 * Lines.Num() + 20), FLinearColor(.03f, .05f, .07f), 5);
						for (int32 N = 0; N < Lines.Num(); ++N)
							Text(At + FVector2D(12, 10 + 22 * N), Lines[N], 14,
							     FLinearColor::White, 6);
					}
				}
		if (MenuOpen)
		{
			Box(MenuPosition, FVector2D(250, MenuHeight()), FLinearColor(.045f, .065f, .09f), 5);
			const TCHAR* Labels[] = {TEXT("快捷转移"), TEXT("装备 / 卸下"), TEXT("使用"), TEXT("拆分数量…"), TEXT("丢弃")};
			for (int32 Row = 0; Row < 5; ++Row)
				Text(MenuPosition + FVector2D(12, 10 + Row * 32), Labels[Row], 15,
				     FLinearColor::White, 6);
			const auto Actions = EquipmentActions();
			for (int32 N = 0; N < Actions.Num(); ++N)
				Text(MenuPosition + FVector2D(12, 10 + (N + 5) * 32),
				     Actions[N].Label, 14, FLinearColor::White, 6);
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

	// 按弹窗、菜单、拖拽和选中顺序处理鼠标，避免一次点击同时触发多个操作。
	virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override
	{
		if (!Interaction() || !Interaction()->InventoryOpen) return FReply::Unhandled();
		Mouse = Position(G, E);
		if (Splitting) return FReply::Handled().SetUserFocus(AsShared());
		if (MenuOpen && E.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			const int32 Row = FMath::FloorToInt((Mouse.Y - MenuPosition.Y - 5) / 32);
			MenuOpen = false;
			if (Inside(Mouse, MenuPosition, FVector2D(250, MenuHeight())) && Row >= 0 && Row < 5 + EquipmentActions().
				Num())
				MenuAction(Row);
			return FReply::Handled(); // Preserve focus if the action opened the gunsmith popup.
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

	// 键盘输入同时考虑拆分框和菜单状态；旋转只改变拖拽预览，落点提交后才由服务器执行。
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
		return SelectedLoot || !C
			       ? TArray<ExtractionItems::FEquipmentAction>()
			       : ExtractionItems::EquipmentActions(*C, Selected);
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

	// 鼠标位置先转换到局部坐标，再撤销绘制时的居中偏移和缩放，使点击与格子对齐。
	FVector2D Position(const FGeometry& G, const FPointerEvent& E) const
	{
		return (G.AbsoluteToLocal(E.GetScreenSpacePosition()) - ExtractionUIStyle::Offset(G)) / ExtractionUIStyle::Scale(G);
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

	// 封装物品 ID、来源、数量及落点；控制器补齐会话、请求序号和容器版本后发送。
	FExtractionItemRequest Request() const
	{
		FExtractionItemRequest R;
		R.Item = Selected;
		R.FromLoot = SelectedLoot;
		R.Quantity = Quantity;
		R.Rotated = Rotated;
		return R;
	}

	// 快捷移动根据当前是否打开战利品容器选择目标；没有目标容器时走装备或收纳路径。
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

	// 在装备槽与背包之间切换；本实现的枪械快捷装备分支按 AK 定义判断。
	void EquipOrStore()
	{
		const auto* C = Container(SelectedLoot);
		const auto* Item = C ? C->Find(Selected) : nullptr;
		if (!Item || Item->Definition != TEXT("AK"))
		{
			PC()->ShowItemMessage(TEXT("只有武器可以装备"));
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
				PC()->ShowItemMessage(TEXT("武器槽已满，请先移出原武器"));
				Cancel();
				return;
			}
		}
		PC()->RequestItem(R);
		Cancel();
	}

	// 把菜单选择转换为物品操作或改枪面板回调，菜单自身不直接扣减物品。
	void MenuAction(int32 Row)
	{
		if (Row >= 5)
		{
			const auto Actions = EquipmentActions();
			if (Actions.IsValidIndex(Row - 5))
			{
				if (Actions[Row - 5].OpenGunsmith)
				{
					TWeakPtr<SExtractionInventoryPanel> Weak = StaticCastSharedRef<SExtractionInventoryPanel>(
						AsShared());
					ExtractionGunsmith::Open(AsShared(), Selected,
					                         [Weak]
					                         {
						                         const auto P = Weak.Pin();
						                         const auto* C = P && P->Interaction() && P->Interaction()->InventoryOpen ? P->Container(false) : nullptr;
						                         return C ? *C : FExtractionContainer();
					                         },
					                         [Weak](const FExtractionItemRequest& R)
					                         {
						                         if (const auto P = Weak.Pin(); P && P->PC()) P->PC()->RequestItem(R);
					                         });
				}
				else PC()->RequestItem(Actions[Row - 5].Request);
			}
			Cancel();
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
			if (Quantity <= 1)
			{
				PC()->ShowItemMessage(TEXT("该物品无法拆分"));
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

	// 将拖拽落点解析为装备槽、格子或合并目标；这里只构造请求，不预先移动真实物品。
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

// 创建 Slate 面板并传入所属 UWidget，面板通过控制器读取当前局内状态。
TSharedRef<SWidget> UExtractionInventoryWidget::RebuildWidget()
{
	return SAssignNew(Panel, SExtractionInventoryPanel).Owner(this);
}

// 释放 Slate 共享引用，避免 UWidget 重建后继续持有旧面板。
void UExtractionInventoryWidget::ReleaseSlateResources(bool ReleaseChildren)
{
	Super::ReleaseSlateResources(ReleaseChildren);
	Panel.Reset();
}

// 根据背包开关调整可见性并刷新面板，关闭时保留不拦截鼠标的提示显示。
void UExtractionInventoryWidget::Refresh()
{
	const auto* PC = Cast<AExtractionPlayerController>(GetOwningPlayer());
	const auto* I = PC ? PC->Interaction() : nullptr;
	SetVisibility(I && I->InventoryOpen ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
	if (Panel) Panel->Refresh();
}

// 将外部取消操作转交面板，统一清理拖拽和弹出菜单状态。
bool UExtractionInventoryWidget::CancelDrag() { return Panel && Panel->Cancel(); }
