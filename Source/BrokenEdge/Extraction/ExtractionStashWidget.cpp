#include "ExtractionStashWidget.h"
#include "ExtractionAccountComponent.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Input/Reply.h"

namespace
{
	constexpr float Cell = 38;
	FVector2D Grid(int32 Side) { return Side == 0 ? FVector2D(105, 165) : FVector2D(660, 245); }
	FVector2D Slot(int32 Index) { return FVector2D(660 + Index * 185, 165); }

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

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Draw,
	                      int32 Layer, const FWidgetStyle&, bool) const override
	{
		const auto* A = Account();
		if (!A) return Layer;
		const float Scale = FMath::Min(G.GetLocalSize().X / 1280, G.GetLocalSize().Y / 720);
		auto Box = [&](FVector2D P, FVector2D S, FLinearColor C, int32 Z = 0)
		{
			FSlateDrawElement::MakeBox(Draw, Layer + Z, G.ToPaintGeometry(S * Scale, FSlateLayoutTransform(P * Scale)),
			                           FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, C);
		};
		auto Text = [&](FVector2D P, const FString& T, int32 Font = 16, int32 Z = 1)
		{
			FSlateDrawElement::MakeText(Draw, Layer + Z,
			                            G.ToPaintGeometry(FVector2D(1100, 60) * Scale,
			                                              FSlateLayoutTransform(P * Scale)), T,
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
			if (Side) for (int32 I = 0; I < 2; ++I)
			{
				Box(Slot(I), {172, 58}, FLinearColor(.09, .15, .18));
				Text(Slot(I) + FVector2D(5, 15), I ? TEXT("背负武器") : TEXT("手持武器"));
			}
			for (const auto& I : C->Items)
			{
				if (Dragging && Selected == I.Id) continue;
				const auto* D = UExtractionItemDefinition::Get(I.Definition);
				const FVector2D P = Origin(I, Side), S = Size(I);
				Box(P + FVector2D(1, 1), S - FVector2D(3, 3),
				    I.Definition == TEXT("AK") ? FLinearColor(.30, .31, .24) : FLinearColor(.16, .32, .34));
				const FString Label = D ? D->Label.ToString() : I.Definition.ToString();
				Text(P + FVector2D(4, 4),
				     S.X < 75 ? (I.Definition == TEXT("Ammo") ? TEXT("弹药") : Label.Left(2)) : Label,
				     S.X < 75 ? 11 : 13);
				Text(P + FVector2D(4, 21),
				     I.Definition == TEXT("AK")
					     ? FString::Printf(TEXT("%d / 30"), I.Magazine)
					     : FString::Printf(TEXT("× %d"), I.Quantity), 12);
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
		if (Menu)
		{
			Box(MenuAt, {200, 160}, FLinearColor(.06, .1, .14), 5);
			const TCHAR* Labels[] = {TEXT("快捷转移"), TEXT("装备到手持槽"), TEXT("装备到背负槽"), TEXT("卸下到背包"), TEXT("拆分数量")};
			for (int32 I = 0; I < 5; ++I) Text(MenuAt + FVector2D(10, 5 + I * 31), Labels[I], 15, 6);
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

	virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent& E) override
	{
		if (Account()) OverflowOffset = FMath::Clamp(OverflowOffset - FMath::RoundToInt(E.GetWheelDelta()) * 5, 0,
		                                             FMath::Max(0, Account()->Profile.Overflow.Num() - 5));
		return FReply::Handled();
	}

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
			if (!InsideStash(Mouse, MenuAt, {200, 160})) return FReply::Handled();
			const int32 Row = FMath::FloorToInt((Mouse.Y - MenuAt.Y) / 31);
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
			MenuAt = {FMath::Min(Mouse.X, 1020.f), FMath::Min(Mouse.Y, 460.f)};
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

	virtual FReply OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E) override
	{
		if (!Dragging || E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
		Mouse = Position(G, E);
		auto R = Request();
		R.Move.AutoPlace = false;
		for (int32 I = 0; I < 2; ++I) if (InsideStash(Mouse, Slot(I), {172, 58}))
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
			if (E.GetKey() == EKeys::Up || E.GetKey() == EKeys::Down) SplitText = FString::FromInt(
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

	void Send(FExtractionStorageRequest R)
	{
		Account()->RequestMove(R);
		Cancel();
	}

	FVector2D Position(const FGeometry& G, const FPointerEvent& E) const
	{
		return G.AbsoluteToLocal(E.GetScreenSpacePosition()) / FMath::Max(
			.1f, FMath::Min(G.GetLocalSize().X / 1280, G.GetLocalSize().Y / 720));
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
		for (int32 S = 0; S < 2; ++S) if (const auto* C = Container(S)) for (const auto& I : C->Items) if (InsideStash(
			P, Origin(I, S), Size(I)))
		{
			Side = S;
			return &I;
		}
		return nullptr;
	}
};

UExtractionAccountComponent* UExtractionStashWidget::Account() const
{
	return GetOwningPlayer() ? GetOwningPlayer()->FindComponentByClass<UExtractionAccountComponent>() : nullptr;
}

TSharedRef<SWidget> UExtractionStashWidget::RebuildWidget()
{
	return SAssignNew(Panel, SExtractionStashPanel).Owner(this);
}

void UExtractionStashWidget::ReleaseSlateResources(bool Children)
{
	Super::ReleaseSlateResources(Children);
	Panel.Reset();
}
