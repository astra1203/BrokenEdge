#include "BrokenEdge/Extraction/UI/ExtractionStashWidget.h"
#include "BrokenEdge/Extraction/UI/ExtractionUIStyle.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionGunsmith.h"
#include "BrokenEdge/Extraction/Core/ExtractionAccountComponent.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Input/Reply.h"
#include "Framework/Application/SlateApplication.h"
// 大厅仓库界面读取 Account 的档案快照，展示仓库、出战装备和溢出物品。
// 界面负责拖拽与选择，Account 负责带版本号的请求；锁定档案期间禁止编辑，防止与出战结算竞争。

namespace
{
	// 仓库 38 px 一格；出战携行网格最多胸挂 4 行 + 背包 8 行 + 安全箱 2 行，缩到 30 px 才放得下。
	constexpr float Cell = 38;
	constexpr float LoadoutCell = 30;
	const FVector2D SlotSize(150, 40);
	const TCHAR* SlotNames[] = {
		TEXT("手持武器"), TEXT("背负武器"), TEXT("头盔"), TEXT("胸甲"), TEXT("胸挂"), TEXT("背包"), TEXT("安全箱")
	};

	FVector2D Slot(int32 Index) { return FVector2D(520, 165 + Index * 46); }

	bool InsideStash(FVector2D P, FVector2D O, FVector2D S)
	{
		return P.X >= O.X && P.Y >= O.Y && P.X < O.X + S.X && P.Y < O.Y + S.Y;
	}

	// 一块网格在面板上的位置：仓库只有一块；出战配置按胸挂、背包、安全箱自上而下排列，各留一行标题。
	struct FStashGrid
	{
		ExtractionItems::FGrid Grid;
		FVector2D At;
		float CellSize = Cell;
		FVector2D Extent() const { return FVector2D(Grid.Width, Grid.Rows()) * CellSize; }
	};

	TArray<FStashGrid> Layout(const FExtractionContainer* C, int32 Side)
	{
		TArray<FStashGrid> Result;
		if (!C) return Result;
		float Y = 165;
		for (const auto& Grid : ExtractionItems::Grids(*C))
		{
			if (Side == 0)
			{
				Result.Add({Grid, FVector2D(105, 165), Cell});
				continue;
			}
			Y += 18;
			Result.Add({Grid, FVector2D(690, Y), LoadoutCell});
			Y += Grid.Rows() * LoadoutCell + 6;
		}
		return Result;
	}

	FString StashGridTitle(const FExtractionContainer& C, const ExtractionItems::FGrid& Grid)
	{
		const auto* Gear = C.InSlot(ExtractionGear::SlotForGrid(Grid.Section));
		const auto* D = Gear ? UExtractionItemDefinition::Get(Gear->Definition) : nullptr;
		return FString::Printf(TEXT("%s %s · %d 格"), Grid.Section == ExtractionGear::RigGrid
			                                             ? TEXT("胸挂")
			                                             : Grid.Section == ExtractionGear::SafeGrid
			                                             ? TEXT("安全箱(死亡不掉落)")
			                                             : TEXT("背包"), D ? *D->Label.ToString() : TEXT(""),
		                       Grid.Cells);
	}
}

class SExtractionStashPanel : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SExtractionStashPanel)
		{
		}

		SLATE_ARGUMENT(UExtractionStashWidget*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& A) { Owner = A._Owner; }
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1280, 720); }
	UExtractionAccountComponent* Account() const { return Owner.IsValid() ? Owner->Account() : nullptr; }

	const FExtractionContainer* Container(int32 Side) const
	{
		return !Account() ? nullptr : Side == 0 ? &Account()->Profile.Stash : &Account()->Profile.Loadout;
	}

	// 物品左上角与尺寸：装备槽用固定卡片，网格物品按所在分区的原点与格子大小换算；分区不存在时放到画面外。
	FVector2D Origin(const FExtractionItem& I, int32 Side) const { return OriginIn(Container(Side), I, Side); }

	static FVector2D OriginIn(const FExtractionContainer* C, const FExtractionItem& I, int32 Side)
	{
		if (I.Slot >= 0) return Slot(I.Slot);
		for (const auto& L : Layout(C, Side))
			if (L.Grid.Section == I.Slot) return L.At + FVector2D(I.Cell.X, I.Cell.Y) * L.CellSize;
		return FVector2D(-10000);
	}

	FVector2D Size(const FExtractionItem& I, int32 Side) const
	{
		if (I.Slot >= 0) return SlotSize;
		return FVector2D(I.Extent().X, I.Extent().Y) * (Side == 0 ? Cell : LoadoutCell);
	}

	// 从当前档案绘制两侧容器和分页溢出区；拖拽时隐藏原位置仅是视觉反馈。
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Draw,
	                      int32 Layer, const FWidgetStyle&, bool) const override
	{
		const auto* A = Account();
		if (!A) return Layer;
		const float Scale = ExtractionUIStyle::Scale(G);
        const FVector2D CanvasOffset = ExtractionUIStyle::Offset(G);
		auto Box = [&](FVector2D P, FVector2D S, FLinearColor C, int32 Z = 0)
		{
			FSlateDrawElement::MakeBox(Draw, Layer + Z, G.ToPaintGeometry(S * Scale, FSlateLayoutTransform(CanvasOffset + P * Scale)),
			                           FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, C);
		};
		auto Text = [&](FVector2D P, const FString& T, int32 Font = 16, int32 Z = 1)
		{
			FSlateDrawElement::MakeText(Draw, Layer + Z,
			                            G.ToPaintGeometry(FVector2D(1100, 60) * Scale,
			                                              FSlateLayoutTransform(CanvasOffset + P * Scale)), T,
			                            ExtractionUIStyle::Font(
				                            FMath::Max(8, FMath::RoundToInt(Font * Scale))),
			                            ESlateDrawEffect::None, FLinearColor::White);
		};
		Box({50, 45}, {1180, 630}, FLinearColor(.018, .03, .045, 1));
		Text({100, 65}, TEXT("仓库与出战装备"), 28);
		Text({105, 126}, TEXT("仓库 · 10 × 12"));
		int32 LoadoutCells = 0;
		if (const auto* L = Container(1)) for (const auto& Grid : ExtractionItems::Grids(*L)) LoadoutCells += Grid.Cells;
		Text({520, 125}, FString::Printf(TEXT("出战装备 · 携行共 %d 格"), LoadoutCells));
		Box({1070, 65}, {115, 42}, FLinearColor(.12, .25, .32));
		Text({1090, 75}, TEXT("返回大厅"));
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const auto* C = Container(Side);
			if (!C) continue;
			for (const auto& L : Layout(C, Side))
			{
				if (Side) Text(L.At - FVector2D(0, 17), StashGridTitle(*C, L.Grid), 11);
				for (int32 Y = 0; Y < L.Grid.Rows(); ++Y)
					for (int32 X = 0; X < L.Grid.Width; ++X)
						if (L.Grid.Has(X, Y))
							Box(L.At + FVector2D(X, Y) * L.CellSize, FVector2D(L.CellSize - 2),
							    FLinearColor(.085, .12, .15));
				// 分仓：口袋之间画一道琥珀分隔线（四边细框）。
				for (const FIntRect& P : L.Grid.Pockets)
				{
					const FVector2D Corner = L.At + FVector2D(P.Min.X, P.Min.Y) * L.CellSize - FVector2D(1);
					const FVector2D S = FVector2D(P.Width(), P.Height()) * L.CellSize;
					const FLinearColor Edge(.95, .66, .23, .55);
					Box(Corner, FVector2D(S.X, 1), Edge, 1);
					Box(Corner + FVector2D(0, S.Y - 1), FVector2D(S.X, 1), Edge, 1);
					Box(Corner, FVector2D(1, S.Y), Edge, 1);
					Box(Corner + FVector2D(S.X - 1, 0), FVector2D(1, S.Y), Edge, 1);
				}
			}
			if (Side)
				for (int32 I = 0; I <= ExtractionGear::SafeSlot; ++I)
				{
					Box(Slot(I), SlotSize, FLinearColor(.09, .15, .18));
					Text(Slot(I) + FVector2D(5, 11), SlotNames[I], 13);
				}
			for (const auto& I : C->Items)
			{
				if (Dragging && Selected == I.Id) continue;
				const auto* D = UExtractionItemDefinition::Get(I.Definition);
				const FVector2D P = Origin(I, Side), S = Size(I, Side);
				Box(P + FVector2D(1, 1), S - FVector2D(3, 3), FLinearColor(.035f, .055f, .06f));
				Box(P + FVector2D(1, 1), FVector2D(S.X - 3, 2), ExtractionUIStyle::Rarity(D ? D->DisplayRarity : 0), 1);
				const bool HasIcon = ExtractionUIStyle::Icon(Draw, Layer + 1, G, P + FVector2D(3, 3),
				                                             S - FVector2D(6, 17), Scale, D);
				const FString Label = D ? D->Label.ToString() : I.Definition.ToString();
				if (!HasIcon) Text(P + FVector2D(4, 4), S.X < 75 ? Label.Left(2) : Label.Left(12), S.X < 75 ? 10 : 12);
				Text(P + FVector2D(4, S.Y - 15),
				     I.Slot >= 0 ? Label.Left(12) : FString::Printf(TEXT("×%d"), I.Quantity), S.X < 75 ? 9 : 11, 2);
			}
		}
		Text({900, 405}, FString::Printf(TEXT("溢出领取区 · %d 件"), A->Profile.Overflow.Num()), 14);
		Text({900, 430}, TEXT("点击领取到仓库，滚轮翻页"), 11);
		for (int32 Row = 0; Row < 5; ++Row)
		{
			const int32 N = OverflowOffset + Row;
			if (!A->Profile.Overflow.IsValidIndex(N)) break;
			const auto& I = A->Profile.Overflow[N];
			const auto* D = UExtractionItemDefinition::Get(I.Definition);
			Box({900, 453.f + Row * 29}, {280, 27}, FLinearColor(.11, .18, .21));
			Text({912, 457.f + Row * 29},
			     FString::Printf(TEXT("%s × %d"), D ? *D->Label.ToString() : *I.Definition.ToString(), I.Quantity), 14);
		}
		Text({105, 630}, A->Locked ? TEXT("已准备或正在保存 · 装备已锁定") : TEXT("拖放整理 · R 旋转 · Shift 点击转移 · 右键装备／拆分 · Esc 返回"),
		     14);
		if (!A->Message.IsEmpty()) Text({900, 610}, A->Message.Left(22), 13);
		if (Dragging)
		{
			const auto* I = Container(SelectedSide)->Find(Selected);
			if (I)
			{
				// 落点预览只用本地快照模拟，最终以服务器为准：绿色移动、蓝色交换（蓝框是换下物品的去处）、琥珀合并、红色放不下。
				FExtractionStorageRequest R;
				FVector2D DropAt, DropSize;
				FString Hint;
				if (Resolve(Mouse, R, DropAt, DropSize))
				{
					const int32 ToSide = static_cast<int32>(R.To);
					FExtractionContainer From = *Container(SelectedSide);
					FExtractionContainer To = *Container(ToSide);
					FString Error;
					const bool bValid = ExtractionItems::Transfer(From, ToSide == SelectedSide ? From : To, R.Move, Error);
					const bool bSwap = R.Move.Op == EExtractionItemOp::Swap;
					const FLinearColor Color = !bValid ? ExtractionUIStyle::Hex(TEXT("EA5A50"))
						: bSwap ? ExtractionUIStyle::Hex(TEXT("57B7EC"))
						: R.Move.MergeInto.IsValid() ? ExtractionUIStyle::Accent() : ExtractionUIStyle::Hex(TEXT("5DBB72"));
					Box(DropAt, DropSize, Color.CopyWithNewOpacity(.35f), 2);
					if (bValid && bSwap)
						if (const auto* Back = From.Find(R.Move.SwapWith))
							Box(OriginIn(&From, *Back, SelectedSide), Size(*Back, SelectedSide),
							    Color.CopyWithNewOpacity(.25f), 2);
					Hint = !bValid ? Error : bSwap ? TEXT("交换位置") : R.Move.MergeInto.IsValid() ? TEXT("合并堆叠") : FString();
				}
				const FIntPoint E = Rotated ? FIntPoint(I->Size.Y, I->Size.X) : I->Size;
				const FVector2D Floating = Mouse - GrabFraction * FVector2D(E.X, E.Y) * Cell;
				Box(Floating, FVector2D(E.X, E.Y) * Cell, FLinearColor(.25, .65, .65, .8), 3);
				Text(Floating, FString::Printf(TEXT("× %d · R 旋转"), Quantity), 14, 4);
				if (!Hint.IsEmpty()) Text(Floating + FVector2D(0, E.Y * Cell + 4), Hint, 14, 4);
			}
		}
		if (Menu && !Dragging)
			if (const auto* C = Container(SelectedSide))
				if (const auto* Item = C->Find(Selected))
				{
					const auto Lines = ExtractionItems::WeaponDetails(*Item);
					if (!Lines.IsEmpty())
					{
						const FVector2D At(MenuAt.X > 640 ? 100 : 740, 80);
						Box(At, FVector2D(440, 22 * Lines.Num() + 20), FLinearColor(.03f, .05f, .07f), 5);
						for (int32 N = 0; N < Lines.Num(); ++N) Text(At + FVector2D(12, 10 + 22 * N), Lines[N], 14, 6);
					}
				}
		if (Menu)
		{
			Box(MenuAt, {250, MenuHeight()}, FLinearColor(.06, .1, .14), 5);
			const bool Gear = SelectedGearSlot() >= 0;
			const TCHAR* Labels[] = {
				TEXT("快捷转移"), Gear ? TEXT("装备到出战") : TEXT("装备到手持槽"), Gear ? TEXT("—") : TEXT("装备到背负槽"),
				TEXT("放入胸挂／背包"), TEXT("拆分数量")
			};
			for (int32 I = 0; I < 5; ++I) Text(MenuAt + FVector2D(10, 5 + I * 31), Labels[I], 15, 6);
			const auto Actions = EquipmentActions();
			for (int32 I = 0; I < Actions.Num(); ++I)
				Text(MenuAt + FVector2D(10, 5 + (I + 5) * 31), Actions[I].Label,
				     14, 6);
		}
		if (Splitting)
		{
			Box({430, 270}, {430, 145}, FLinearColor(.08, .14, .18), 7);
			Text({450, 292}, TEXT("拆分数量：") + SplitText, 22, 8);
			Text({450, 343}, TEXT("输入数字 / ↑↓ · Enter 拖放 · Esc 取消"), 16, 8);
		}
		return Layer + 9;
	}

	virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override
	{
		Mouse = Position(G, E);
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	// 滚轮调整溢出区显示位置，一次按五项移动，不改变溢出物品的存储顺序。
	virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent& E) override
	{
		if (Account())
			OverflowOffset = FMath::Clamp(OverflowOffset - FMath::RoundToInt(E.GetWheelDelta()) * 5, 0,
			                              FMath::Max(0, Account()->Profile.Overflow.Num() - 5));
		return FReply::Handled();
	}

	// 先处理返回和档案锁，再解析菜单、溢出回收、快捷转移与拖拽。
	virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override
	{
		Mouse = Position(G, E);
		if (InsideStash(Mouse, {1070, 65}, {115, 42}))
		{
			Owner->RemoveFromParent();
			return FReply::Handled();
		}
		if (!Account() || Account()->Locked) return FReply::Handled();
		if (Menu)
		{
			Menu = false;
			if (!InsideStash(Mouse, MenuAt, {250, MenuHeight()})) return FReply::Handled();
			const int32 Row = FMath::FloorToInt((Mouse.Y - MenuAt.Y) / 31);
			if (Row >= 5)
			{
				const auto Actions = EquipmentActions();
				if (Actions.IsValidIndex(Row - 5))
				{
					if (Actions[Row - 5].OpenGunsmith)
					{
						TWeakPtr<SExtractionStashPanel> Weak = StaticCastSharedRef<SExtractionStashPanel>(AsShared());
						const int32 Side = SelectedSide;
						ExtractionGunsmith::Open(AsShared(), Selected,
						                         [Weak, Side]
						                         {
							                         const auto P = Weak.Pin();
							                         const auto* C = P && P->Owner.IsValid() && P->Owner->IsInViewport()
								                                         ? P->Container(Side)
								                                         : nullptr;
							                         return C ? *C : FExtractionContainer();
						                         },
						                         [Weak, Side](const FExtractionItemRequest& Move)
						                         {
							                         if (const auto P = Weak.Pin(); P && P->Account())
							                         {
								                         FExtractionStorageRequest R;
								                         R.From = R.To = static_cast<EExtractionStorageSide>(Side);
								                         R.Move = Move;
								                         P->Send(R);
							                         }
						                         });
					}
					else
					{
						auto R = Request();
						R.To = R.From;
						R.Move = Actions[Row - 5].Request;
						Send(R);
					}
				}
				return FReply::Handled();
			}
			if (Row == 4 && Quantity > 1)
			{
				SplitText = FString::FromInt(Quantity / 2);
				Splitting = true;
			}
			else if (Row < 4)
			{
				const int32 GearSlot = SelectedGearSlot();
				if (GearSlot >= 0 && Row == 2) return FReply::Handled().SetUserFocus(AsShared());
				auto R = Request();
				R.To = Row == 0 ? OtherSide() : EExtractionStorageSide::Loadout;
				// 胸挂/背包/安全箱"装备到出战"直接进对应装备槽；武器仍是手持/背负两个槽。
				R.Move.Slot = Row == 1 ? (GearSlot >= 0 ? GearSlot : 0) : Row == 2 ? 1 : -1;
				Send(R);
			}
			return FReply::Handled().SetUserFocus(AsShared());
		}
		if (Splitting) return FReply::Handled();
		if (InsideStash(Mouse, {900, 453}, {280, 145}))
		{
			const int32 N = OverflowOffset + FMath::FloorToInt((Mouse.Y - 453) / 29);
			if (Account()->Profile.Overflow.IsValidIndex(N))
			{
				FExtractionStorageRequest R;
				R.From = EExtractionStorageSide::Overflow;
				R.To = EExtractionStorageSide::Stash;
				R.Move.Item = Account()->Profile.Overflow[N].Id;
				R.Move.Quantity = Account()->Profile.Overflow[N].Quantity;
				Send(R);
			}
			return FReply::Handled();
		}
		const auto* I = Hit(Mouse, SelectedSide);
		if (!I) return FReply::Handled();
		Selected = I->Id;
		Quantity = I->Quantity;
		Rotated = I->Rotated;
		// 记住按下点在物品上的相对位置，拖动时物品跟着这一点走，不会跳到鼠标右下方；装备槽卡片按中心抓取。
		GrabFraction = I->Slot >= 0
			               ? FVector2D(.5f)
			               : ((Mouse - Origin(*I, SelectedSide)) / Size(*I, SelectedSide)).ClampAxes(0.f, .999f);
		if (E.GetEffectingButton() == EKeys::RightMouseButton)
		{
			MenuAt = {FMath::Min(Mouse.X, 960.f), FMath::Min(Mouse.Y, 690.f - MenuHeight())};
			Menu = true;
		}
		else if (E.IsShiftDown())
		{
			auto R = Request();
			R.To = OtherSide();
			Send(R);
		}
		else
		{
			Dragging = true;
			return FReply::Handled().CaptureMouse(AsShared()).SetUserFocus(AsShared());
		}
		return FReply::Handled().SetUserFocus(AsShared());
	}

	// 释放鼠标时根据落点生成移动或合并请求；服务器拒绝时仍以同步档案为准。
	virtual FReply OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E) override
	{
		if (!Dragging || E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
		Mouse = Position(G, E);
		FExtractionStorageRequest R;
		FVector2D DropAt, DropSize;
		if (Resolve(Mouse, R, DropAt, DropSize)) Send(R);
		else Cancel();
		return FReply::Handled().ReleaseMouseCapture();
	}

	virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& E) override
	{
		if (E.GetKey() == EKeys::Escape)
		{
			if (Dragging || Menu || Splitting) Cancel();
			else Owner->RemoveFromParent();
			return FReply::Handled().ReleaseMouseCapture();
		}
		if (Splitting)
		{
			if (E.GetKey() == EKeys::BackSpace) SplitText.LeftChopInline(1);
			if (E.GetKey() == EKeys::Up || E.GetKey() == EKeys::Down)
				SplitText = FString::FromInt(
					FMath::Clamp(FCString::Atoi(*SplitText) + (E.GetKey() == EKeys::Up ? 1 : -1), 1, Quantity - 1));
			if (E.GetKey() == EKeys::Enter)
			{
				Quantity = FMath::Clamp(FCString::Atoi(*SplitText), 1, Quantity - 1);
				Splitting = false;
				Dragging = true;
			}
			return FReply::Handled();
		}
		if (Dragging && E.GetKey() == EKeys::R) Rotated = !Rotated;
		return FReply::Handled();
	}

	virtual FReply OnKeyChar(const FGeometry&, const FCharacterEvent& E) override
	{
		if (Splitting && E.GetCharacter() >= '0' && E.GetCharacter() <= '9')
		{
			if (SplitText.Len() >= 3) SplitText.Reset();
			SplitText.AppendChar(E.GetCharacter());
		}
		return FReply::Handled();
	}

private:
	TArray<ExtractionItems::FEquipmentAction> EquipmentActions() const
	{
		const auto* C = Container(SelectedSide);
		return C ? ExtractionItems::EquipmentActions(*C, Selected) : TArray<ExtractionItems::FEquipmentAction>();
	}

	float MenuHeight() const { return 5.f + 31.f * (5 + EquipmentActions().Num()); }

	// 把鼠标落点换算成请求（只读，不提交）；松手和拖动预览共用。At/Size 是落点在面板上的矩形。
	// 装备槽有别的物品时交换；网格里鼠标下是同类未满堆叠时合并，是别的物品时交换；
	// 鼠标在空格但占位只压到一件物品时，把那件换出去。拆分出的部分数量不交换。
	bool Resolve(FVector2D P, FExtractionStorageRequest& R, FVector2D& DropAt, FVector2D& DropSize) const
	{
		const auto* Source = Container(SelectedSide);
		const auto* Item = Source ? Source->Find(Selected) : nullptr;
		if (!Item) return false;
		R = Request();
		R.Move.AutoPlace = false;
		const bool bWhole = Quantity == Item->Quantity;
		auto SwapWith = [&R](const FExtractionItem& Other)
		{
			R.Move.Op = EExtractionItemOp::Swap;
			R.Move.SwapWith = Other.Id;
		};
		for (int32 I = 0; I <= ExtractionGear::SafeSlot; ++I)
			if (InsideStash(P, Slot(I), SlotSize))
			{
				R.To = EExtractionStorageSide::Loadout;
				R.Move.Slot = I;
				if (const auto* Loadout = Container(1))
					if (const auto* Occupant = Loadout->InSlot(I); bWhole && Occupant && Occupant->Id != Selected)
						SwapWith(*Occupant);
				DropAt = Slot(I);
				DropSize = SlotSize;
				return true;
			}
		const FIntPoint Ext = Rotated ? FIntPoint(Item->Size.Y, Item->Size.X) : Item->Size;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const auto* Target = Container(Side);
			for (const auto& L : Layout(Target, Side))
			{
				if (!InsideStash(P, L.At, L.Extent())) continue;
				R.To = static_cast<EExtractionStorageSide>(Side);
				R.Move.Slot = L.Grid.Section;
				// 抓取点换算成物品左上角，再取最近的格子。
				const FVector2D InGrid = (P - L.At) / L.CellSize;
				const FVector2D Corner = InGrid - GrabFraction * FVector2D(Ext.X, Ext.Y);
				R.Move.Cell = FIntPoint(FMath::RoundToInt(Corner.X), FMath::RoundToInt(Corner.Y));
				bool bHovered = false;
				for (const auto& Other : Target->Items)
					if (Other.Id != Selected && Other.Slot == L.Grid.Section && InGrid.X >= Other.Cell.X
						&& InGrid.Y >= Other.Cell.Y && InGrid.X < Other.Cell.X + Other.Extent().X
						&& InGrid.Y < Other.Cell.Y + Other.Extent().Y)
					{
						bHovered = true;
						const auto* D = UExtractionItemDefinition::Get(Item->Definition);
						if (D && Other.Definition == Item->Definition && D->MaxStack > Other.Quantity)
							R.Move.MergeInto = Other.Id;
						else if (bWhole)
						{
							SwapWith(Other);
							R.Move.Cell = Other.Cell;
						}
						break;
					}
				if (!bHovered && bWhole)
				{
					const FExtractionItem* Only = nullptr;
					int32 Overlaps = 0;
					for (const auto& Other : Target->Items)
						if (Other.Id != Selected && Other.Slot == L.Grid.Section
							&& R.Move.Cell.X < Other.Cell.X + Other.Extent().X && R.Move.Cell.X + Ext.X > Other.Cell.X
							&& R.Move.Cell.Y < Other.Cell.Y + Other.Extent().Y && R.Move.Cell.Y + Ext.Y > Other.Cell.Y)
						{
							Only = &Other;
							++Overlaps;
						}
					if (Overlaps == 1) SwapWith(*Only);
				}
				DropAt = L.At + FVector2D(R.Move.Cell.X, R.Move.Cell.Y) * L.CellSize;
				DropSize = FVector2D(Ext.X, Ext.Y) * L.CellSize;
				return true;
			}
		}
		return false;
	}

	// 选中物品若是胸挂/背包/安全箱，返回它对应的装备槽，否则 -1。
	int32 SelectedGearSlot() const
	{
		const auto* C = Container(SelectedSide);
		const auto* I = C ? C->Find(Selected) : nullptr;
		const auto* D = I ? UExtractionItemDefinition::Get(I->Definition) : nullptr;
		return D && D->Kind == EExtractionItemKind::Gear ? D->GearSlot : -1;
	}
	TWeakObjectPtr<UExtractionStashWidget> Owner;
	FGuid Selected;
	int32 SelectedSide = 0, Quantity = 1, OverflowOffset = 0;
	bool Dragging = false, Menu = false, Splitting = false, Rotated = false;
	FString SplitText;
	FVector2D Mouse = FVector2D::ZeroVector, MenuAt = FVector2D::ZeroVector;
	/** 抓取点在物品上的相对位置（0..1）。 */
	FVector2D GrabFraction = FVector2D::ZeroVector;

	void Cancel()
	{
		Dragging = Menu = Splitting = false;
		Selected.Invalidate();
	}

	// 通过 Account 提交操作并取消本地选择；请求 ID 和档案版本由账号组件统一设置。
	void Send(FExtractionStorageRequest R)
	{
		Account()->RequestMove(R);
		Cancel();
	}

	// 撤销界面缩放与居中偏移，使用与绘制一致的设计坐标做命中测试。
	FVector2D Position(const FGeometry& G, const FPointerEvent& E) const
	{
		return (G.AbsoluteToLocal(E.GetScreenSpacePosition()) - ExtractionUIStyle::Offset(G)) / ExtractionUIStyle::Scale(G);
	}

	EExtractionStorageSide OtherSide() const
	{
		return SelectedSide == 0 ? EExtractionStorageSide::Loadout : EExtractionStorageSide::Stash;
	}

	FExtractionStorageRequest Request() const
	{
		FExtractionStorageRequest R;
		R.From = static_cast<EExtractionStorageSide>(SelectedSide);
		R.Move.Item = Selected;
		R.Move.Quantity = Quantity;
		R.Move.Rotated = Rotated;
		return R;
	}

	const FExtractionItem* Hit(FVector2D P, int32& Side) const
	{
		for (int32 S = 0; S < 2; ++S)
			if (const auto* C = Container(S))
				for (const auto& I : C->Items)
					if (InsideStash(
						P, Origin(I, S), Size(I, S)))
					{
						Side = S;
						return &I;
					}
		return nullptr;
	}
};

// 从所属玩家控制器查找账号组件，所有仓库操作使用同一份同步档案。
UExtractionAccountComponent* UExtractionStashWidget::Account() const
{
	return GetOwningPlayer() ? GetOwningPlayer()->FindComponentByClass<UExtractionAccountComponent>() : nullptr;
}

// 创建仓库 Slate 面板，保留 UMG 生命周期管理入口。
TSharedRef<SWidget> UExtractionStashWidget::RebuildWidget()
{
	return SAssignNew(Panel, SExtractionStashPanel).Owner(this);
}

// 把键盘焦点交给仓库面板，以接收旋转、拆分和退出操作。
void UExtractionStashWidget::FocusPanel()
{
	if (Panel.IsValid()) FSlateApplication::Get().SetKeyboardFocus(Panel, EFocusCause::SetDirectly);
}

// 释放面板引用，支持关闭或重建界面。
void UExtractionStashWidget::ReleaseSlateResources(bool Children)
{
	Super::ReleaseSlateResources(Children);
	Panel.Reset();
}
