#include "ExtractionProgressionWidget.h"
#include "ExtractionUIStyle.h"
#include "ExtractionAccountComponent.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Input/Reply.h"
#include "Framework/Application/SlateApplication.h"
// 商店与任务页共用一个 Slate 面板，读取账号快照和成长规则展示报价、等级及任务状态。
// 确认窗保存预览时的档案版本，服务器仍会重新校验价格、库存、任务条件并持久化事务。

namespace
{
	bool IsGun(FName Id)
	{
		const auto* Definition = UExtractionItemDefinition::Get(Id);
		return Definition && Definition->Kind == EExtractionItemKind::Weapon;
	}

	bool Contains(FVector2D P, FVector2D O, FVector2D Size)
	{
		return P.X >= O.X && P.Y >= O.Y && P.X < O.X + Size.X && P.Y < O.Y + Size.Y;
	}

	FString ItemLabel(FName Id)
	{
		const auto* D = UExtractionItemDefinition::Get(Id);
		return D ? D->Label.ToString() : Id.ToString();
	}

	// 商品名后缀：枪械标空弹匣，钥匙卡标可用开门次数；其他物品不加后缀。
	FString OfferSuffix(FName Id)
	{
		const auto* D = UExtractionItemDefinition::Get(Id);
		if (!D) return {};
		if (D->Kind == EExtractionItemKind::Weapon) return TEXT("（空弹匣）");
		if (D->Kind == EExtractionItemKind::Key) return FString::Printf(TEXT("（开门 %d 次）"), D->KeyUses);
		return {};
	}
}

class SExtractionProgressionPanel : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SExtractionProgressionPanel)
		{
		}

		SLATE_ARGUMENT(UExtractionProgressionWidget*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& A)
	{
		Owner = A._Owner;
		Tasks = Owner->TasksInitially;
	}

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FVector2D ComputeDesiredSize(float) const override { return {1280, 720}; }
	UExtractionAccountComponent* Account() const { return Owner.IsValid() ? Owner->Account() : nullptr; }

	// 从规则与档案绘制商品、回收列表或任务；确认报价只用于展示，不是扣款依据。
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Draw,
	                      int32 Layer, const FWidgetStyle&, bool) const override
	{
		const auto* A = Account();
		if (!A) return Layer;
		const auto* R = GetDefault<UExtractionProgressionRules>();
		const auto& P = A->Profile.Progression;
		const float Scale = ExtractionUIStyle::Scale(G);
        const FVector2D CanvasOffset = ExtractionUIStyle::Offset(G);
		auto Box = [&](FVector2D At, FVector2D Size, FLinearColor Color, int32 Z = 0)
		{
			FSlateDrawElement::MakeBox(Draw, Layer + Z,
			                           G.ToPaintGeometry(Size * Scale, FSlateLayoutTransform(CanvasOffset + At * Scale)),
			                           FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Color);
		};
		auto Text = [&](FVector2D At, const FString& Value, int32 Font = 16, int32 Z = 1)
		{
			FSlateDrawElement::MakeText(Draw, Layer + Z,
			                            G.ToPaintGeometry(FVector2D(1160, 70) * Scale,
			                                              FSlateLayoutTransform(CanvasOffset + At * Scale)),
			                            Value, FCoreStyle::GetDefaultFontStyle(
				                            "Regular", FMath::Max(8, FMath::RoundToInt(Font * Scale))),
			                            ESlateDrawEffect::None, FLinearColor::White);
		};
		const FLinearColor Button(.1, .25, .32), Selected(.12, .42, .39);
		Box({0, 0}, {1280, 720}, FLinearColor(.008, .015, .023, .99));
		Text({65, 35}, Tasks ? TEXT("行动任务") : TEXT("物资商店"), 30);
		Text({65, 85}, ExtractionProgression::Summary(P), 20);
		for (int32 I = 0; I < 2; ++I)
		{
			Box({730.f + I * 145, 45}, {130, 42}, Tasks == bool(I) ? Selected : Button);
			Text({765.f + I * 145, 55}, I ? TEXT("任务") : TEXT("商店"), 18);
		}
		Box({1040, 45}, {165, 42}, Button);
		Text({1060, 55}, TEXT("返回大厅"), 18);
		if (Tasks)
		{
			for (int32 I = 0; I < R->Quests.Num() && P.Quests.IsValidIndex(I); ++I)
			{
				const auto& D = R->Quests[I];
				const auto& Q = P.Quests[I];
				const float Y = 145.f + I * 150;
				Box({65, Y}, {1140, 132}, FLinearColor(.035, .065, .085));
				Text({85, Y + 12}, FString::Printf(TEXT("%d  %s"), I + 1, *D.Label), 22);
				Text({85, Y + 47}, D.Description, 16);
				FString State = Q.Claimed
					                ? TEXT("已领奖")
					                : !Q.Accepted
					                ? TEXT("未接取")
					                : FString::Printf(TEXT("进度 %d / %d"), Q.Progress, D.Target);
				if (I == 2 && Q.Accepted && !Q.Delivered) State += FString::Printf(
					TEXT(" · 已记录带出 %d 份文件"), Q.EligibleFiles.Num());
				Text({85, Y + 80}, FString::Printf(TEXT("%s   ·   奖励 %d 货币 / %d 经验"), *State, D.Credits, D.Experience),
				     16);
				Box({1000, Y + 42}, {175, 44}, Button);
				const bool Locked = !Q.Accepted && (R->Level(P.Experience) < I + 1 || (I && !P.Quests[I - 1].Claimed));
				Text({1015, Y + 54},
				     Q.Claimed
					     ? TEXT("已完成")
					     : Locked
					     ? TEXT("前置任务未完成")
					     : !Q.Accepted
					     ? TEXT("接取任务")
					     : I == 2 && !Q.Delivered
					     ? TEXT("提交仓库文件")
					     : TEXT("领取奖励"), 16);
			}
			Text({65, 610}, TEXT("个人任务 · 成功撤离保存本局进度 · 文件需从领取区移入仓库后提交"), 17);
		}
		else
		{
			Text({65, 132}, TEXT("购买 · 滚轮翻页"), 20);
			Text({660, 132}, TEXT("仓库回收 · 点击选中 · 滚轮翻页"), 20);
			for (int32 Row = 0; Row < 8; ++Row)
			{
				const float Y = 170.f + Row * 44;
				if (R->Offers.IsValidIndex(OfferOffset + Row))
				{
					const auto& O = R->Offers[OfferOffset + Row];
					Box({65, Y}, {550, 40},
					    !Selling && SelectedOffer == O.Definition ? Selected : FLinearColor(.04, .075, .10));
					Text({78, Y + 10}, FString::Printf(
						     TEXT("%s%s  ·  %d / 件  ·  Lv.%d%s"), *ItemLabel(O.Definition),
						     *OfferSuffix(O.Definition),
						     O.Price, O.Level, R->Level(P.Experience) < O.Level ? TEXT("  未解锁") : TEXT("")), 16);
				}
				if (A->Profile.Stash.Items.IsValidIndex(SellOffset + Row))
				{
					const auto& I = A->Profile.Stash.Items[SellOffset + Row];
					Box({650, Y}, {555, 40}, Selling && SelectedItem == I.Id ? Selected : FLinearColor(.04, .075, .10));
					const int64 Price = R->SellPrice(I, I.Quantity);
					Text({665, Y + 9}, FString::Printf(
						     TEXT("%s × %d  ·  %s  ·  %s"), *ItemLabel(I.Definition), I.Quantity,
						     *ExtractionItems::Details(I),
						     Price < 0 ? TEXT("不可回收") : *FString::Printf(TEXT("共 %lld"), Price)), 15);
				}
			}
			Text({65, 538}, FString::Printf(TEXT("数量  %d   ·   数字键输入 / ↑↓调整 / Shift 加减 10"), Quantity), 17);
			Box({65, 577}, {550, 48}, Button);
			Text({85, 590}, Selling ? TEXT("查看出售明细") : TEXT("查看购买明细"), 19);
			Text({650, 548}, TEXT("整枪含弹药和附件 · 护甲按剩余耐久折价 · 钥匙卡按开门次数"), 16);
			Text({650, 583}, TEXT("仅操作仓库 · 钥匙卡购入后移入配装即可带入战局"), 16);
		}
		Text({65, 661}, A->Locked
			                ? TEXT("已准备、切换模式或存储不可用 · 当前禁止修改")
			                : LocalMessage.IsEmpty()
			                ? A->Message.Left(65)
			                : LocalMessage.Left(65), 16);
		if (Confirm)
		{
			Box({200, 205}, {880, 300}, FLinearColor(.055, .10, .13), 5);
			Text({225, 225}, Pending.Action == EExtractionEconomyAction::Buy ? TEXT("确认购买") : TEXT("确认出售"), 25, 6);
			Text({225, 275}, Detail, 18, 6);
			Text({225, 320}, Parts, 16, 6);
			Text({225, 365}, FString::Printf(TEXT("数量 %d  ·  合计 %lld 货币"), Pending.Quantity, Quote), 22, 6);
			Box({600, 430}, {175, 44}, Button, 6);
			Text({650, 442}, TEXT("确认"), 18, 7);
			Box({800, 430}, {175, 44}, Button, 6);
			Text({850, 442}, TEXT("取消"), 18, 7);
		}
		return Layer + 8;
	}

	FVector2D Position(const FGeometry& G, const FPointerEvent& E) const
	{
		return (G.AbsoluteToLocal(E.GetScreenSpacePosition()) - ExtractionUIStyle::Offset(G)) / ExtractionUIStyle::Scale(G);
	}

	// 在非确认状态下分别滚动商品与回收列表，任务页不复用商店滚动行为。
	virtual FReply OnMouseWheel(const FGeometry& G, const FPointerEvent& E) override
	{
		if (!Tasks && !Confirm && Account())
		{
			const int32 Step = E.GetWheelDelta() > 0 ? -1 : 1;
			if (Position(G, E).X < 630) OfferOffset = FMath::Clamp(OfferOffset + Step, 0,
			                                                       FMath::Max(
				                                                       0, GetDefault<UExtractionProgressionRules>()->
				                                                          Offers.Num() - 8));
			else SellOffset = FMath::Clamp(SellOffset + Step, 0,
			                               FMath::Max(0, Account()->Profile.Stash.Items.Num() - 8));
		}
		return FReply::Handled();
	}

	// 确认前比较预览版本与当前档案，过期预览不能直接提交；其他点击切换标签或生成任务请求。
	virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override
	{
		auto* A = Account();
		if (!A) return FReply::Handled();
		const FVector2D P = Position(G, E);
		const auto* R = GetDefault<UExtractionProgressionRules>();
		LocalMessage.Reset();
		if (Confirm)
		{
			if (Contains(P, {800, 430}, {175, 44})) Confirm = false;
			else if (Contains(P, {600, 430}, {175, 44}))
			{
				Confirm = false;
				if (Pending.Revision != A->Profile.Revision) LocalMessage = TEXT("仓库已更新，请重新查看交易明细");
				else A->RequestEconomy(Pending);
			}
		}
		else if (Contains(P, {1040, 45}, {165, 42})) Owner->RemoveFromParent();
		else if (Contains(P, {730, 45}, {130, 42})) Tasks = false;
		else if (Contains(P, {875, 45}, {130, 42})) Tasks = true;
		else if (Tasks && !A->Locked)
		{
			for (int32 I = 0; I < 3; ++I)
				if (A->Profile.Progression.Quests.IsValidIndex(I) && Contains(P, {1000, 187.f + I * 150}, {175, 44}))
				{
					const auto& Q = A->Profile.Progression.Quests[I];
					FExtractionEconomyRequest Request;
					Request.Quest = I;
					Request.Action = !Q.Accepted
						                 ? EExtractionEconomyAction::AcceptQuest
						                 : I == 2 && !Q.Delivered
						                 ? EExtractionEconomyAction::DeliverQuest
						                 : EExtractionEconomyAction::ClaimQuest;
					A->RequestEconomy(Request);
				}
		}
		else if (!Tasks)
		{
			for (int32 Row = 0; Row < 8; ++Row)
			{
				if (Contains(P, {65, 170.f + Row * 44}, {550, 40}) && R->Offers.IsValidIndex(OfferOffset + Row))
				{
					Selling = false;
					SelectedOffer = R->Offers[OfferOffset + Row].Definition;
					Quantity = 1;
					EditingQuantity = false;
				}
				if (Contains(P, {650, 170.f + Row * 44}, {555, 40}) && A->Profile.Stash.Items.IsValidIndex(
					SellOffset + Row))
				{
					Selling = true;
					SelectedItem = A->Profile.Stash.Items[SellOffset + Row].Id;
					Quantity = 1;
					EditingQuantity = false;
				}
			}
			if (!A->Locked && Contains(P, {65, 577}, {550, 48})) Preview();
		}
		return FReply::Handled().SetUserFocus(AsShared());
	}

	// 处理取消和数量调整，避免确认窗与底层页面同时响应退出。
	virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& E) override
	{
		if (E.GetKey() == EKeys::Escape)
		{
			if (Confirm) Confirm = false;
			else if (Owner.IsValid()) Owner->RemoveFromParent();
		}
		if (!Confirm && (E.GetKey() == EKeys::Up || E.GetKey() == EKeys::Down)) Quantity = FMath::Clamp(
			Quantity + (E.GetKey() == EKeys::Up ? 1 : -1) * (E.IsShiftDown() ? 10 : 1), 1, 600);
		if (!Confirm && E.GetKey() == EKeys::BackSpace)
		{
			Quantity = FMath::Max(1, Quantity / 10);
			EditingQuantity = false;
		}
		return FReply::Handled();
	}

	virtual FReply OnKeyChar(const FGeometry&, const FCharacterEvent& E) override
	{
		const TCHAR C = E.GetCharacter();
		if (!Tasks && !Confirm && C >= '0' && C <= '9')
		{
			Quantity = FMath::Clamp((EditingQuantity ? Quantity * 10 : 0) + C - '0', 1, 600);
			EditingQuantity = true;
		}
		return FReply::Handled();
	}

private:
	TWeakObjectPtr<UExtractionProgressionWidget> Owner;
	bool Tasks = false, Selling = false, Confirm = false, EditingQuantity = false;
	int32 OfferOffset = 0, SellOffset = 0, Quantity = 1;
	FName SelectedOffer = TEXT("AK");
	FGuid SelectedItem;
	FExtractionEconomyRequest Pending;
	int64 Quote = 0;
	FString LocalMessage, Detail, Parts;

	// 基于当前档案保存待确认请求和版本，并计算显示报价；整枪回收包含弹药和附件价值。
	void Preview()
	{
		const auto* A = Account();
		const auto* R = GetDefault<UExtractionProgressionRules>();
		Pending = FExtractionEconomyRequest();
		Pending.Revision = A->Profile.Revision;
		Pending.Quantity = Quantity;
		Parts.Reset();
		if (Selling)
		{
			const auto* I = A->Profile.Stash.Find(SelectedItem);
			if (!I || (Quote = R->SellPrice(*I, Quantity)) < 0)
			{
				LocalMessage = TEXT("请选择可回收物品并检查数量");
				return;
			}
			Pending.Action = EExtractionEconomyAction::Sell;
			Pending.Item = I->Id;
			Detail = ItemLabel(I->Definition) + TEXT(" · ") + ExtractionItems::Details(*I);
			if (IsGun(I->Definition))
			{
				Parts = FString::Printf(TEXT("随枪出售：%s × %d"), *ItemLabel(I->MagazineAmmo), I->Magazine);
				for (const auto& Part : I->Attachments) Parts += TEXT(" · ") + ItemLabel(Part.Definition);
			}
		}
		else
		{
			const auto* O = R->Offer(SelectedOffer);
			if (!O) return;
			Pending.Definition = SelectedOffer;
			Quote = int64(O->Price) * Quantity;
			const auto* D = UExtractionItemDefinition::Get(SelectedOffer);
			const bool bKey = D && D->Kind == EExtractionItemKind::Key;
			Detail = ItemLabel(SelectedOffer) + (IsGun(SelectedOffer) ? TEXT(" · 空弹匣，无附件") : TEXT(""))
				+ (bKey ? FString::Printf(TEXT(" · 可开门 %d 次"), D->KeyUses) : TEXT(""));
			Parts = bKey
				        ? TEXT("钥匙卡存入仓库；移入配装后带入战局，每次开门消耗 1 次")
				        : TEXT("物资存入仓库；空间不足时整笔取消");
		}
		Confirm = true;
	}
};

// 取得本地玩家的账号组件，用于读取档案和发送经济请求。
UExtractionAccountComponent* UExtractionProgressionWidget::Account() const
{
	return GetOwningPlayer() ? GetOwningPlayer()->FindComponentByClass<UExtractionAccountComponent>() : nullptr;
}

// 创建面板并传入初始标签页选择。
TSharedRef<SWidget> UExtractionProgressionWidget::RebuildWidget()
{
	return SAssignNew(Panel, SExtractionProgressionPanel).Owner(this);
}

// 设置面板键盘焦点，支持数量输入和取消确认。
void UExtractionProgressionWidget::FocusPanel()
{
	if (Panel.IsValid()) FSlateApplication::Get().SetKeyboardFocus(Panel, EFocusCause::SetDirectly);
}

// 释放 Slate 面板的共享引用。
void UExtractionProgressionWidget::ReleaseSlateResources(bool Children)
{
	Super::ReleaseSlateResources(Children);
	Panel.Reset();
}
