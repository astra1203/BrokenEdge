#include "BrokenEdge/Extraction/Inventory/ExtractionGunsmith.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Styling/CoreStyle.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
// 改枪面板通过 Read/Submit 回调复用局内背包和大厅仓库的数据入口，不持有可直接修改的真实容器。
// 属性对比在容器副本上执行 Configure；点击应用才发送请求，并等待版本变化或等待提示超时。

namespace
{
	TWeakPtr<SWidget> ActivePanel;

	// 从视口移除当前面板并清理弱引用，避免重复打开叠加输入层。
	void CloseGunsmith()
	{
		if (GEngine && GEngine->GameViewport)
			if (const auto Panel = ActivePanel.Pin()) GEngine->GameViewport->RemoveViewportWidgetContent(
				Panel.ToSharedRef());
		ActivePanel.Reset();
	}

	// 读取武器机制数据中的固定总成信息，供界面展示；固定件与可操作的物品附件分开处理。
	TArray<FString> FixedParts(FName Definition)
	{
		// Display the explicit snapshot assembly only. These are already part of the base mesh/stats.
		TArray<FString> Lines;
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(
				Text, *(FPaths::ProjectContentDir() / TEXT("Extraction/Data/WeaponMechanics.json")))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root))
			return Lines;
		const TSharedPtr<FJsonObject>* Profile;
		if (!Root->GetObjectField(TEXT("profiles"))->TryGetObjectField(Definition.ToString(), Profile)) return Lines;
		TSet<int32> Seen;
		for (const auto& Entry : (*Profile)->GetObjectField(TEXT("assembly"))->Values)
		{
			const auto Row = Entry.Value->AsObject();
			const int32 Index = Row->GetIntegerField(TEXT("Index"));
			if (Seen.Contains(Index)) continue;
			Seen.Add(Index);
			const int32 Socket = Row->GetIntegerField(TEXT("ParentSocketId"));
			const TCHAR* Name = Socket == 1
				                    ? TEXT("手枪握把")
				                    : Socket == 2
				                    ? TEXT("枪管")
				                    : Socket == 3
				                    ? TEXT("枪托")
				                    : Socket == 4
				                    ? TEXT("护木")
				                    : Socket == 5
				                    ? TEXT("弹匣")
				                    : TEXT("机匣");
			Lines.Add(FString::Printf(TEXT("  ├ %s（标准固定件）"), Name));
		}
		return Lines;
	}

	class SGunsmith : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SGunsmith)
			{
			}

		SLATE_END_ARGS()

		virtual bool SupportsKeyboardFocus() const override { return true; }

		virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& Event) override
		{
			if (Event.GetKey() == EKeys::Escape)
			{
				CloseGunsmith();
				return FReply::Handled();
			}
			return FReply::Unhandled();
		}

		// 保存目标武器 ID 及读写回调，初次构建可执行操作和属性预览。
		void Construct(const FArguments&, FGuid InWeapon, TFunction<FExtractionContainer()> InRead,
		               TFunction<void(const FExtractionItemRequest&)> InSubmit)
		{
			Weapon = InWeapon;
			Read = MoveTemp(InRead);
			Submit = MoveTemp(InSubmit);
			Rebuild();
		}

		// 重新读取快照：武器消失则关闭，版本变化则重建；等待超时只解除界面等待，不证明事务失败。
		virtual void Tick(const FGeometry& G, double Now, float Delta) override
		{
			SCompoundWidget::Tick(G, Now, Delta);
			const auto C = Read();
			if (!C.Find(Weapon))
			{
				CloseGunsmith();
				return;
			}
			if (C.Revision != Revision || (Pending && Now - SentAt > 3))
			{
				Pending = false;
				Selected = INDEX_NONE;
				Rebuild();
			}
		}

	private:
		FGuid Weapon;
		TFunction<FExtractionContainer()> Read;
		TFunction<void(const FExtractionItemRequest&)> Submit;
		TArray<ExtractionItems::FEquipmentAction> Actions;
		TSharedPtr<STextBlock> Preview;
		int32 Revision = -1, Selected = INDEX_NONE;
		bool Pending = false, Valid = false;
		double SentAt = 0;

		// 重新枚举装卸和调校操作，在副本上预演并显示前后属性；等待回执期间禁用重复应用。
		void Rebuild()
		{
			const auto C = Read();
			Revision = C.Revision;
			const auto* Gun = C.Find(Weapon);
			if (!Gun) return;
			Actions = ExtractionItems::EquipmentActions(C, Weapon, true);
			for (const auto& Item : C.Items)
				if (const auto* D = UExtractionItemDefinition::Get(Item.Definition); D && D->Kind ==
					EExtractionItemKind::Attachment)
					for (const auto& Action : ExtractionItems::EquipmentActions(C, Item.Id, true))
						if (Action.Request.Weapon == Weapon) Actions.Add(Action);
			TArray<FString> Lines = ExtractionItems::WeaponDetails(*Gun);
			Lines.Append(FixedParts(Gun->Definition));
			TSharedPtr<SScrollBox> Buttons;
			ChildSlot[SNew(SBox).WidthOverride(1000).HeightOverride(580)
			[
				SNew(SBorder).Padding(16).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.03f, .05f, .07f, 1.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("枪械改装 · 选择操作预览，然后应用")))
					]
					+ SVerticalBox::Slot().FillHeight(1).Padding(0, 12)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1)
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
							[
								SNew(STextBlock).Text(FText::FromString(FString::Join(Lines, TEXT("\n")))).WrapTextAt(
									460)
							]
							+ SScrollBox::Slot().Padding(0, 18)
							[
								SAssignNew(Preview, STextBlock)
								.Text(FText::FromString(TEXT("选择右侧操作查看变化。\n固定基础件已计入整枪，不可独立拆装。")))
								.WrapTextAt(460)
							]
						]
						+ SHorizontalBox::Slot().FillWidth(1).Padding(12, 0)
						[
							SAssignNew(Buttons, SScrollBox)
						]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("应用预览操作")))
							.IsEnabled_Lambda([this] { return Valid && !Pending; })
							.OnClicked_Lambda([this]
							{
								if (Actions.IsValidIndex(Selected))
								{
									Pending = true;
									SentAt = FSlateApplication::Get().GetCurrentTime();
									Submit(Actions[Selected].Request);
								}
								return FReply::Handled();
							})
						]
						+ SHorizontalBox::Slot().FillWidth(1)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("关闭")))
							.OnClicked_Lambda([]
							{
								CloseGunsmith();
								return FReply::Handled();
							})
						]
					]
				]
			]];
			Valid = false;
			for (int32 I = 0; I < Actions.Num(); ++I)
				Buttons->AddSlot().Padding(0, 3)[SNew(SButton)
				.Text(FText::FromString(Actions[I].Label))
				.IsEnabled_Lambda([this] { return !Pending; })
				.OnClicked_Lambda([this, I]
				{
					Selected = I;
					auto Before = Read();
					auto After = Before;
					FString Error;
					Valid = ExtractionItems::Configure(After, Actions[I].Request, Error);
					TArray<FString> Lines;
					if (Valid && After.Find(Weapon))
					{
						const auto Old = ExtractionItems::WeaponDetails(*Before.Find(Weapon));
						const auto New = ExtractionItems::WeaponDetails(*After.Find(Weapon));
						Lines.Add(TEXT("预览（尚未应用）"));
						for (int32 N = 1; N < FMath::Min(5, New.Num()); ++N)
							if (Old[N] != New[N])
							{
								Lines.Add(TEXT("原：") + Old[N]);
								Lines.Add(TEXT("新：") + New[N]);
							}
						if (Lines.Num() == 1) Lines.Add(TEXT("显示属性不变；装配或弹药状态将更新。"));
					}
					else Lines.Add(TEXT("无法应用：") + Error);
					Preview->SetText(FText::FromString(FString::Join(Lines, TEXT("\n"))));
					return FReply::Handled();
				})];
		}
	};
}

// 关闭已有改枪面板，在游戏视口创建居中的新面板并设置键盘焦点。
void ExtractionGunsmith::Open(TSharedRef<SWidget> Parent, FGuid Weapon, TFunction<FExtractionContainer()> Read,
                              TFunction<void(const FExtractionItemRequest&)> Submit)
{
	if (!GEngine || !GEngine->GameViewport) return;
	CloseGunsmith();
	const auto Content = SNew(SGunsmith, Weapon, MoveTemp(Read), MoveTemp(Submit));
	const auto Panel = SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SDPIScaler).DPIScale_Lambda([] { return 1.f / FMath::Max(.1f, UWidgetLayoutLibrary::GetViewportScale(GEngine->GameViewport)); })[Content]
		];
	ActivePanel = Panel;
	GEngine->GameViewport->AddViewportWidgetContent(Panel, 100);
	FSlateApplication::Get().SetKeyboardFocus(Content);
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_GUNSMITH_OPEN %s"), *Weapon.ToString());
}
