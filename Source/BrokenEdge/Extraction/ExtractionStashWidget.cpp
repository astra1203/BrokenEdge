#include "ExtractionStashWidget.h"
#include "ExtractionUIStyle.h"
#include "ExtractionGunsmith.h"
#include "ExtractionAccountComponent.h"
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
	constexpr float Cell = 38;
	FVector2D Grid(int32 Side) { return Side == 0 ? FVector2D(105, 165) : FVector2D(660, 245); }

	FVector2D Slot(int32 Index)
	{
		return Index < 2 ? FVector2D(660 + Index * 185, 165) : FVector2D(1040, 165 + (Index - 2) * 80);
	}

	bool InsideStash(FVector2D P, FVector2D O, FVector2D S)
	{
		return P.X >= O.X && P.Y >= O.Y && P.X < O.X + S.X && P.Y < O.Y + S.Y;
	}

	FVector2D Origin(const FExtractionItem& I, int32 Side)
	{
		return I.Slot >= 0 ? Slot(I.Slot) : Grid(Side) + FVector2D(I.Cell.X, I.Cell.Y) * Cell;
	}

	FVector2D Size(const FExtractionItem& I)
	{
		return I.Slot >= 0 ? FVector2D(172, 58) : FVector2D(I.Extent().X, I.Extent().Y) * Cell;
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
			                            FCoreStyle::GetDefaultFontStyle(
				                            "Regular", FMath::Max(8, FMath::RoundToInt(Font * Scale))),
			                            ESlateDrawEffect::None, FLinearColor::White);
		};
		Box({50, 45}, {1180, 630}, FLinearColor(.018, .03, .045, 1));
		Text({100, 65}, TEXT("仓库与出战装备"), 28);
		Text({105, 126}, TEXT("仓库 · 10 × 12"));
		Text({660, 125}, TEXT("出战装备 · 背包 6 × 4 ＋双武器槽"));
		Box({1070, 65}, {115, 42}, FLinearColor(.12, .25, .32));
		Text({1090, 75}, TEXT("返回大厅"));
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const auto* C = Container(Side);
			if (!C) continue;
			for (int32 Y = 0; Y < C->Height; ++Y)
				for (int32 X = 0; X < C->Width; ++X)
					Box(Grid(Side) + FVector2D(X, Y) * Cell, {Cell - 2, Cell - 2}, FLinearColor(.085, .12, .15));
			if (Side)
				for (int32 I = 0; I < 4; ++I)
				{
					Box(Slot(I), {172, 58}, FLinearColor(.09, .15, .18));
					Text(Slot(I) + FVector2D(5, 15),
					     I == 0 ? TEXT("手持武器") : I == 1 ? TEXT("背负武器") : I == 2 ? TEXT("头盔") : TEXT("胸甲"));
				}
			for (const auto& I : C->Items)
			{
				if (Dragging && Selected == I.Id) continue;
				const auto* D = UExtractionItemDefinition::Get(I.Definition);
				const FVector2D P = Origin(I, Side), S = Size(I);
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
		Text({660, 425}, FString::Printf(TEXT("溢出领取区 · %d 件（点击领取到仓库，滚轮翻页）"), A->Profile.Overflow.Num()), 14);
		for (int32 Row = 0; Row < 5; ++Row)
		{
			const int32 N = OverflowOffset + Row;
			if (!A->Profile.Overflow.IsValidIndex(N)) break;
			const auto& I = A->Profile.Overflow[N];
			const auto* D = UExtractionItemDefinition::Get(I.Definition);
			Box({660, 453.f + Row * 29}, {495, 27}, FLinearColor(.11, .18, .21));
			Text({672, 457.f + Row * 29},
			     FString::Printf(TEXT("%s × %d"), D ? *D->Label.ToString() : *I.Definition.ToString(), I.Quantity), 14);
		}
		Text({105, 630}, A->Locked ? TEXT("已准备或正在保存 · 装备已锁定") : TEXT("拖放整理 · R 旋转 · Shift 点击转移 · 右键装备／拆分 · Esc 返回"),
		     14);
		if (!A->Message.IsEmpty()) Text({660, 605}, A->Message.Left(44), 13);
		if (Dragging)
		{
			const auto* I = Container(SelectedSide)->Find(Selected);
			if (I)
			{
				const FIntPoint E = Rotated ? FIntPoint(I->Size.Y, I->Size.X) : I->Size;
				Box(Mouse, FVector2D(E.X, E.Y) * Cell, FLinearColor(.25, .65, .65, .8), 3);
				Text(Mouse, FString::Printf(TEXT("× %d · R 旋转"), Quantity), 14, 4);
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
			const TCHAR* Labels[] = {TEXT("快捷转移"), TEXT("装备到手持槽"), TEXT("装备到背负槽"), TEXT("卸下到背包"), TEXT("拆分数量")};
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
				auto R = Request();
				R.To = Row == 0 ? OtherSide() : EExtractionStorageSide::Loadout;
				R.Move.Slot = Row == 1 ? 0 : Row == 2 ? 1 : -1;
				Send(R);
			}
			return FReply::Handled().SetUserFocus(AsShared());
		}
		if (Splitting) return FReply::Handled();
		if (InsideStash(Mouse, {660, 453}, {495, 145}))
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
		auto R = Request();
		R.Move.AutoPlace = false;
		for (int32 I = 0; I < 4; ++I)
			if (InsideStash(Mouse, Slot(I), {172, 58}))
			{
				R.To = EExtractionStorageSide::Loadout;
				R.Move.Slot = I;
				Send(R);
				return FReply::Handled().ReleaseMouseCapture();
			}
		int32 TargetSide = 0;
		const auto* Target = Hit(Mouse, TargetSide);
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const auto* C = Container(Side);
			if (!InsideStash(Mouse, Grid(Side), FVector2D(C->Width, C->Height) * Cell)) continue;
			R.To = static_cast<EExtractionStorageSide>(Side);
			const auto V = (Mouse - Grid(Side)) / Cell;
			R.Move.Cell = FIntPoint(FMath::FloorToInt(V.X), FMath::FloorToInt(V.Y));
			if (Target && TargetSide == Side && Target->Id != Selected) R.Move.MergeInto = Target->Id;
			Send(R);
			return FReply::Handled().ReleaseMouseCapture();
		}
		Cancel();
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
	TWeakObjectPtr<UExtractionStashWidget> Owner;
	FGuid Selected;
	int32 SelectedSide = 0, Quantity = 1, OverflowOffset = 0;
	bool Dragging = false, Menu = false, Splitting = false, Rotated = false;
	FString SplitText;
	FVector2D Mouse = FVector2D::ZeroVector, MenuAt = FVector2D::ZeroVector;

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
						P, Origin(I, S), Size(I)))
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
