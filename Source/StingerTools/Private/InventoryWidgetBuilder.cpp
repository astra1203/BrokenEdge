#include "InventoryWidgetBuilder.h"
#include "BrokenEdge/Extraction/LootingMainView.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "WidgetBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/Button.h"
#include "Components/SpinBox.h"
#include "Components/ProgressBar.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Slate/WidgetRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "ImageUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "RenderingThread.h"
#include "AssetCompilingManager.h"

namespace
{
	const FString Folder = TEXT("/Game/UI/Looting/");
	FLinearColor Surface(.025f, .035f, .04f, .96f), Muted(.55f, .62f, .64f, 1), Accent(.22f, .85f, .7f, 1);

	template <class T>
	T* W(UWidgetTree* Tree, FName Name = NAME_None) { return Tree->ConstructWidget<T>(T::StaticClass(), Name); }

	UTextBlock* Label(UWidgetTree* T, FName Name, const FString& Value, int32 Size = 20,
	                  FLinearColor Color = FLinearColor::White)
	{
		auto* X = W<UTextBlock>(T, Name);
		X->SetText(FText::FromString(Value));
		auto Font = X->GetFont();
		Font.Size = Size;
		X->SetFont(Font);
		X->SetColorAndOpacity(Color);
		X->SetVisibility(ESlateVisibility::HitTestInvisible);
		return X;
	}

	UBorder* Border(UWidgetTree* T, FName Name, FLinearColor Color)
	{
		auto* B = W<UBorder>(T, Name);
		B->SetBrushColor(Color);
		B->SetPadding(FMargin(10));
		return B;
	}

	void At(UCanvasPanel* C, UWidget* X, FVector2D Position, FVector2D Size, int32 Z = 0)
	{
		auto* S = C->AddChildToCanvas(X);
		S->SetPosition(Position);
		S->SetSize(Size);
		S->SetZOrder(Z);
	}

	void Fill(UCanvasPanel* C, UWidget* X, int32 Z = 0)
	{
		auto* S = C->AddChildToCanvas(X);
		S->SetAnchors(FAnchors(0, 0, 1, 1));
		S->SetOffsets(FMargin(0));
		S->SetZOrder(Z);
	}

	UWidgetBlueprint* Create(const TCHAR* Name, UClass* Parent, TFunction<void(UWidgetTree*)> Layout,
	                         TArray<UWidgetBlueprint*>& All)
	{
		const FString Path = Folder + Name;
		if (auto* Existing = LoadObject<UWidgetBlueprint>(nullptr, *(Path + TEXT(".") + Name)))
		{
			All.Add(Existing);
			return Existing;
		}
		auto* Package = CreatePackage(*Path);
		auto* BP = CastChecked<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			Parent, Package, FName(Name), BPTYPE_Normal, UWidgetBlueprint::StaticClass(),
			UWidgetBlueprintGeneratedClass::StaticClass()));
		Layout(BP->WidgetTree);
		FKismetEditorUtilities::CompileBlueprint(BP);
		FAssetRegistryModule::AssetCreated(BP);
		BP->MarkPackageDirty();
		All.Add(BP);
		return BP;
	}

	ULootingWidgetPart* Part(UWidgetTree* T, UWidgetBlueprint* BP, FName Name)
	{
		return T->ConstructWidget<ULootingWidgetPart>(BP->GeneratedClass.Get(), Name);
	}

	bool Save(UWidgetBlueprint* BP)
	{
		FKismetEditorUtilities::CompileBlueprint(BP);
		if (BP->Status == BS_Error) return false;
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(
			BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		return UPackage::SavePackage(BP->GetOutermost(), BP, *Filename, Args);
	}

	// Upgrade only the affected slots; retain the rest of each designer-authored tree.
	bool RepairWeaponLayout(UWidgetBlueprint* Item, UWidgetBlueprint* Equipment, UWidgetBlueprint* Container)
	{
		auto FillOverlay = [](UWidgetBlueprint* BP, const TCHAR* Name)
		{
			auto* Widget = BP->WidgetTree->FindWidget(FName(Name));
			auto* Slot = Widget ? Cast<UOverlaySlot>(Widget->Slot) : nullptr;
			if (!Slot) return false;
			if (Slot->GetHorizontalAlignment() != HAlign_Fill || Slot->GetVerticalAlignment() != VAlign_Fill)
			{
				Slot->SetHorizontalAlignment(HAlign_Fill);
				Slot->SetVerticalAlignment(VAlign_Fill);
				BP->MarkPackageDirty();
			}
			return true;
		};
		auto* Canvas = Cast<UCanvasPanel>(Equipment->WidgetTree->FindWidget(TEXT("ItemCanvas")));
		auto* CanvasSlot = Canvas ? Cast<UOverlaySlot>(Canvas->Slot) : nullptr;
		auto* Header = Cast<UTextBlock>(Equipment->WidgetTree->FindWidget(TEXT("Label")));
		auto* HeaderSlot = Header ? Cast<UOverlaySlot>(Header->Slot) : nullptr;
		auto* Size = Cast<USizeBox>(Equipment->WidgetTree->FindWidget(TEXT("ContentSize")));
		if (!CanvasSlot || !HeaderSlot || !Size) return false;
		const bool LegacyEquipment = CanvasSlot->GetHorizontalAlignment() != HAlign_Fill ||
			CanvasSlot->GetVerticalAlignment() != VAlign_Fill;
		if (!FillOverlay(Equipment, TEXT("ItemCanvas")) || !FillOverlay(Container, TEXT("GridCanvas")) ||
			!FillOverlay(Container, TEXT("ItemCanvas")))
			return false;
		if (LegacyEquipment)
		{
			CanvasSlot->SetPadding(FMargin(0, 26, 0, 0));
			HeaderSlot->SetVerticalAlignment(VAlign_Top);
			Size->SetHeightOverride(FMath::Max(176.f, Size->GetHeightOverride()));
		}
		auto* Icon = Cast<UImage>(Item->WidgetTree->FindWidget(TEXT("Icon")));
		if (!Icon) return false;
		if (!Item->WidgetTree->FindWidget(TEXT("WeaponIconFit")))
		{
			auto* Overlay = Cast<UOverlay>(Icon->GetParent());
			if (!Overlay) return false;
			Icon->RemoveFromParent();
			auto* Fit = W<UScaleBox>(Item->WidgetTree, TEXT("WeaponIconFit"));
			Item->OnVariableAdded(Fit->GetFName());
			Fit->SetStretch(EStretch::ScaleToFit);
			Fit->SetVisibility(ESlateVisibility::HitTestInvisible);
			Fit->SetContent(Icon);
			auto* Inner = CastChecked<UScaleBoxSlot>(Icon->Slot);
			Inner->SetHorizontalAlignment(HAlign_Center);
			Inner->SetVerticalAlignment(VAlign_Center);
			auto* FitSlot = Overlay->AddChildToOverlay(Fit);
			FitSlot->SetHorizontalAlignment(HAlign_Fill);
			FitSlot->SetVerticalAlignment(VAlign_Fill);
			FitSlot->SetPadding(FMargin(8, 24, 8, 24));
			if (auto* Title = Cast<UTextBlock>(Item->WidgetTree->FindWidget(TEXT("Label"))))
				Title->SetAutoWrapText(false);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Item);
		}
		return true;
	}
}

FString UInventoryWidgetBuilder::CreateInventoryWidgets()
{
	TArray<UWidgetBlueprint*> All;
	auto* Item = Create(TEXT("WBP_LootingSlotView"), ULootingWidgetPart::StaticClass(), [](UWidgetTree* T)
	{
		auto* Size = W<USizeBox>(T,TEXT("ContentSize"));
		T->RootWidget = Size;
		auto* BG = Border(T,TEXT("CardBackground"), FLinearColor(.07f, .095f, .10f, 1));
		BG->SetPadding(FMargin(4));
		Size->SetContent(BG);
		auto* O = W<UOverlay>(T);
		BG->SetContent(O);
		auto* Image = W<UImage>(T,TEXT("Icon"));
		Image->SetVisibility(ESlateVisibility::HitTestInvisible);
		O->AddChildToOverlay(Image)->SetPadding(FMargin(8, 18, 8, 24));
		auto* Bar = Border(T,TEXT("RarityBar"), Accent);
		Bar->SetPadding(FMargin(0));
		auto* BS = W<USizeBox>(T);
		BS->SetHeightOverride(3);
		BS->SetContent(Bar);
		O->AddChildToOverlay(BS)->SetVerticalAlignment(VAlign_Top);
		auto* Title = Label(T,TEXT("Label"),TEXT("物品名称"), 13);
		Title->SetAutoWrapText(true);
		auto* TS = O->AddChildToOverlay(Title);
		TS->SetVerticalAlignment(VAlign_Top);
		TS->SetPadding(FMargin(4, 5, 4, 0));
		auto* Detail = Label(T,TEXT("Detail"),TEXT("×1"), 14);
		auto* DS = O->AddChildToOverlay(Detail);
		DS->SetVerticalAlignment(VAlign_Bottom);
		DS->SetHorizontalAlignment(HAlign_Right);
		DS->SetPadding(FMargin(4));
	}, All);
	auto* Cell = Create(TEXT("WBP_LootingGridCell"), ULootingWidgetPart::StaticClass(), [](UWidgetTree* T)
	{
		T->RootWidget = Border(T,TEXT("CellBackground"), FLinearColor(.055f, .075f, .08f, 1));
	}, All);
	auto* Slot = Create(TEXT("WBP_LootEquipSlot"), ULootingWidgetPart::StaticClass(), [](UWidgetTree* T)
	{
		auto* S = W<USizeBox>(T,TEXT("ContentSize"));
		S->SetHeightOverride(108);
		T->RootWidget = S;
		auto* BG = Border(T,TEXT("SlotBackground"), Surface);
		S->SetContent(BG);
		auto* O = W<UOverlay>(T);
		BG->SetContent(O);
		O->AddChildToOverlay(Label(T,TEXT("Label"),TEXT("装备槽"), 18, Muted))->SetVerticalAlignment(VAlign_Center);
		O->AddChildToOverlay(W<UCanvasPanel>(T,TEXT("ItemCanvas")));
	}, All);
	auto* Action = Create(TEXT("WBP_LootingMenuAction"), ULootingWidgetPart::StaticClass(), [](UWidgetTree* T)
	{
		auto* S = W<USizeBox>(T);
		S->SetHeightOverride(42);
		T->RootWidget = S;
		auto* B = W<UButton>(T,TEXT("ActionButton"));
		S->SetContent(B);
		B->SetBackgroundColor(FLinearColor(.06f, .09f, .10f, 1));
		B->SetContent(Label(T,TEXT("Label"),TEXT("操作"), 18));
	}, All);
	auto* Container = Create(TEXT("WBP_LootingBackpackV2_Pc"), ULootingWidgetPart::StaticClass(), [](UWidgetTree* T)
	{
		auto* BG = Border(T,TEXT("ContainerBackground"), Surface);
		T->RootWidget = BG;
		auto* Scroll = W<UScrollBox>(T);
		BG->SetContent(Scroll);
		auto* V = W<UVerticalBox>(T);
		Scroll->AddChild(V);
		V->AddChildToVerticalBox(W<UVerticalBox>(T,TEXT("Entries")));
		auto* Size = W<USizeBox>(T,TEXT("ContentSize"));
		Size->SetWidthOverride(384);
		Size->SetHeightOverride(256);
		V->AddChildToVerticalBox(Size)->SetHorizontalAlignment(HAlign_Left);
		auto* O = W<UOverlay>(T);
		Size->SetContent(O);
		auto* Cells = W<UCanvasPanel>(T,TEXT("GridCanvas"));
		Cells->SetVisibility(ESlateVisibility::HitTestInvisible);
		O->AddChildToOverlay(Cells);
		O->AddChildToOverlay(W<UCanvasPanel>(T,TEXT("ItemCanvas")));
	}, All);
	auto* Menu = Create(TEXT("WBP_LootingContextMenu"), ULootingWidgetPart::StaticClass(), [](UWidgetTree* T)
	{
		auto* S = W<USizeBox>(T);
		S->SetWidthOverride(300);
		S->SetMaxDesiredHeight(700);
		T->RootWidget = S;
		auto* B = Border(T,TEXT("MenuBackground"), FLinearColor(.035f, .055f, .06f, 1));
		S->SetContent(B);
		auto* Scroll = W<UScrollBox>(T);
		B->SetContent(Scroll);
		Scroll->AddChild(W<UVerticalBox>(T,TEXT("Entries")));
	}, All);
	auto* Split = Create(TEXT("WBP_LootingSplitDialog"), ULootingWidgetPart::StaticClass(), [](UWidgetTree* T)
	{
		auto* B = Border(T,TEXT("SplitBackground"), Surface);
		B->SetPadding(FMargin(28));
		T->RootWidget = B;
		auto* V = W<UVerticalBox>(T);
		B->SetContent(V);
		V->AddChildToVerticalBox(Label(T,TEXT("Label"),TEXT("拆分数量"), 26));
		auto* Spin = W<USpinBox>(T,TEXT("SplitAmount"));
		Spin->SetDelta(1);
		Spin->SetMinValue(1);
		Spin->SetMaxValue(99);
		V->AddChildToVerticalBox(Spin)->SetPadding(FMargin(0, 24));
		auto* Button = W<UButton>(T,TEXT("ActionButton"));
		Button->SetContent(Label(T, NAME_None,TEXT("确认并选择目标格子"), 20));
		V->AddChildToVerticalBox(Button);
		V->AddChildToVerticalBox(Label(T,TEXT("Detail"),TEXT("Enter 确认 · Esc 取消"), 16, Muted))->SetPadding(
			FMargin(0, 16, 0, 0));
	}, All);
	auto* Prompt = Create(TEXT("WBP_LootingInteractionPrompt"), ULootingWidgetPart::StaticClass(),
	                      [](UWidgetTree* T)
	                      {
		                      auto* V = W<UVerticalBox>(T);
		                      T->RootWidget = V;
		                      auto* L = Label(T,TEXT("Label"),TEXT("E 交互"), 22);
		                      L->SetJustification(ETextJustify::Center);
		                      V->AddChildToVerticalBox(L);
		                      auto* P = W<UProgressBar>(T,TEXT("Progress"));
		                      P->SetFillColorAndOpacity(Accent);
		                      auto* S = W<USizeBox>(T);
		                      S->SetHeightOverride(6);
		                      S->SetContent(P);
		                      V->AddChildToVerticalBox(S)->SetPadding(FMargin(90, 14));
		                      auto* D = Label(T,TEXT("Detail"),TEXT(""), 18, FLinearColor(1, .7f, .3f, 1));
		                      D->SetJustification(ETextJustify::Center);
		                      V->AddChildToVerticalBox(D);
		                      auto* Hint = Label(T, NAME_None,TEXT("Tab 背包    H 医疗    E 交互    1 当前枪    2 切枪    3 战术刀"),
		                                         16, Muted);
		                      Hint->SetJustification(ETextJustify::Center);
		                      V->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0, 16));
	                      }, All);
	const bool HadRoot = FindPackage(nullptr, *(Folder + TEXT("WBP_LootingPickupView_Pc"))) ||
		FPackageName::DoesPackageExist(Folder + TEXT("WBP_LootingPickupView_Pc"));
	auto* Root = Create(TEXT("WBP_LootingPickupView_Pc"), ULootingMainView::StaticClass(), [&](UWidgetTree* T)
	{
		auto* Scale = W<UScaleBox>(T);
		Scale->SetStretch(EStretch::ScaleToFit);
		T->RootWidget = Scale;
		auto* Size = W<USizeBox>(T);
		Size->SetWidthOverride(1920);
		Size->SetHeightOverride(1080);
		Scale->SetContent(Size);
		auto* Canvas = W<UCanvasPanel>(T);
		Size->SetContent(Canvas);
		auto* Body = W<UCanvasPanel>(T,TEXT("InventoryBody"));
		Fill(Canvas, Body);
		auto* BG = Border(T,TEXT("ScreenBackground"), FLinearColor(.008f, .015f, .02f, .94f));
		Fill(Body, BG);
		At(Body, Label(T, NAME_None,TEXT("战地物资"), 38), FVector2D(80, 65), FVector2D(700, 60));
		At(Body, Label(T, NAME_None,TEXT("装备"), 25, Accent), FVector2D(80, 150), FVector2D(430, 45));
		At(Body, Label(T,TEXT("OwnTitle"),TEXT("背包"), 25, Accent), FVector2D(550, 150), FVector2D(500, 45));
		At(Body, Label(T,TEXT("LootTitle"),TEXT("战利品 / 搜索容器"), 25, Accent), FVector2D(1090, 150), FVector2D(750, 45));
		auto* EquipBG = Border(T,TEXT("EquipmentBackground"), Surface);
		At(Body, EquipBG, FVector2D(80, 210), FVector2D(430, 670));
		auto* Scroll = W<UScrollBox>(T);
		EquipBG->SetContent(Scroll);
		Scroll->AddChild(W<UVerticalBox>(T,TEXT("EquipmentList")));
		At(Body, Part(T, Container,TEXT("OwnContainer")), FVector2D(550, 210), FVector2D(500, 670));
		At(Body, Part(T, Container,TEXT("LootContainer")), FVector2D(1090, 210), FVector2D(750, 670));
		At(Body, Label(T,TEXT("StatusText"),TEXT(""), 20, FLinearColor(1, .7f, .3f, 1)), FVector2D(80, 900),
		   FVector2D(1760, 42));
		At(Body, Label(T, NAME_None,TEXT("拖动放置  ·  R 旋转  ·  Shift 点击快捷转移  ·  右键操作"), 18, Muted), FVector2D(80, 970),
		   FVector2D(1400, 40));
		At(Body, Label(T, NAME_None,TEXT("Tab / Esc 关闭"), 18, Muted), FVector2D(1580, 970), FVector2D(260, 40));
		auto* Popups = W<UCanvasPanel>(T,TEXT("PopupCanvas"));
		Popups->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Fill(Body, Popups, 10);
		auto* Context = Part(T, Menu,TEXT("ContextMenu"));
		auto* CS = Popups->AddChildToCanvas(Context);
		CS->SetAutoSize(true);
		CS->SetZOrder(1);
		Context->SetVisibility(ESlateVisibility::Collapsed);
		auto* Dialog = Part(T, Split,TEXT("SplitDialog"));
		At(Popups, Dialog, FVector2D(710, 350), FVector2D(500, 300), 2);
		Dialog->SetVisibility(ESlateVisibility::Collapsed);
		auto* Hints = Part(T, Prompt,TEXT("InteractionPrompt"));
		Hints->SetVisibility(ESlateVisibility::HitTestInvisible);
		At(Canvas, Hints, FVector2D(460, 820), FVector2D(1000, 230), 20);
	}, All);
	if (!HadRoot)
	{
		auto* CDO = CastChecked<ULootingMainView>(Root->GeneratedClass->GetDefaultObject());
		CDO->ItemClass = Item->GeneratedClass;
		CDO->CellClass = Cell->GeneratedClass;
		CDO->SlotClass = Slot->GeneratedClass;
		CDO->ActionClass = Action->GeneratedClass;
	}
	if (!RepairWeaponLayout(Item, Slot, Container))
		return TEXT(
			"FAILED: unexpected weapon layout; designer tree retained");
	for (auto* BP : All) if (BP->GetOutermost()->IsDirty() && !Save(BP)) return TEXT("FAILED: ") + BP->GetPathName();
	return ValidateInventoryWidgets();
}

FString UInventoryWidgetBuilder::ValidateInventoryWidgets()
{
	auto* BP = LoadObject<UWidgetBlueprint>(
		nullptr, *(Folder + TEXT("WBP_LootingPickupView_Pc.WBP_LootingPickupView_Pc")));
	if (!BP || !BP->GeneratedClass) return TEXT("FAILED: main blueprint missing");
	const TCHAR* Names[] = {
		TEXT("InventoryBody"),TEXT("PopupCanvas"),TEXT("EquipmentList"),TEXT("OwnContainer"),TEXT("LootContainer"),
		TEXT("ContextMenu"),TEXT("SplitDialog"),TEXT("InteractionPrompt"),TEXT("StatusText"),TEXT("LootTitle"),
		TEXT("OwnTitle")
	};
	for (auto Name : Names)
		if (!BP->WidgetTree->FindWidget(FName(Name)))
			return FString(TEXT("FAILED: missing ")) +
				Name;
	auto* D = Cast<ULootingMainView>(BP->GeneratedClass->GetDefaultObject());
	if (!D || !D->ItemClass || !D->CellClass || !D->SlotClass || !D->ActionClass)
		return TEXT(
			"FAILED: template class references");
	FKismetEditorUtilities::CompileBlueprint(BP);
	if (BP->Status == BS_Error) return TEXT("FAILED: blueprint compilation");
	auto* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return TEXT("FAILED: no validation world");
	auto* Instance = CreateWidget<ULootingMainView>(World, BP->GeneratedClass.Get());
	if (!Instance || !Instance->InventoryBody || !Instance->PopupCanvas || !Instance->EquipmentList ||
		!Instance->OwnContainer || !Instance->LootContainer || !Instance->ContextMenu ||
		!Instance->SplitDialog || !Instance->InteractionPrompt || !Instance->StatusText ||
		!Instance->OwnTitle || !Instance->LootTitle)
		return TEXT("FAILED: instantiated BindWidget references");
	// Commandlets have no Slate application; validate UObject bindings without rendering.
	Instance->Refresh();
	if (!Instance->OwnContainer->ItemCanvas || !Instance->OwnContainer->GridCanvas ||
		!Instance->LootContainer->ItemCanvas || !Instance->ContextMenu->Entries ||
		!Instance->SplitDialog->SplitAmount || !Instance->SplitDialog->ActionButton)
		return TEXT("FAILED: nested template bindings");
	auto* Equipment = CreateWidget<ULootingWidgetPart>(World, D->SlotClass);
	auto* Item = CreateWidget<ULootingWidgetPart>(World, D->ItemClass);
	for (auto* Canvas : {
		     Equipment->ItemCanvas.Get(), Instance->OwnContainer->ItemCanvas.Get(),
		     Instance->OwnContainer->GridCanvas.Get(), Instance->LootContainer->ItemCanvas.Get()
	     })
	{
		auto* Slot = Canvas ? Cast<UOverlaySlot>(Canvas->Slot) : nullptr;
		if (!Slot || Slot->GetHorizontalAlignment() != HAlign_Fill || Slot->GetVerticalAlignment() != VAlign_Fill)
			return TEXT("FAILED: inventory canvas must fill its overlay");
	}
	auto* Fit = Item->Icon ? Cast<UScaleBox>(Item->Icon->GetParent()) : nullptr;
	if (!Fit || Fit->GetStretch() != EStretch::ScaleToFit) return TEXT("FAILED: image aspect ratio layout");
	return TEXT(
		"PASSED: saved designer tree, template references, blueprint compilation and instantiated widget bindings");
}

FString UInventoryWidgetBuilder::RenderWeaponLayoutPreview()
{
	if (!FSlateApplication::IsInitialized()) return TEXT("FAILED: render preview requires full editor, not commandlet");
	auto* World = GEditor->GetEditorWorldContext().World();
	auto Load = [](const TCHAR* Name)
	{
		return LoadObject<UWidgetBlueprint>(nullptr, *(Folder + Name + TEXT(".") + Name));
	};
	auto* ItemBP = Load(TEXT("WBP_LootingSlotView"));
	auto* EquipBP = Load(TEXT("WBP_LootEquipSlot"));
	auto* ContainerBP = Load(TEXT("WBP_LootingBackpackV2_Pc"));
	if (!ItemBP || !EquipBP || !ContainerBP) return TEXT("FAILED: missing preview assets");
	auto* Container = CreateWidget<ULootingWidgetPart>(World, ContainerBP->GeneratedClass.Get());
	Container->ContentSize->SetWidthOverride(512);
	Container->ContentSize->SetHeightOverride(256);
	TSharedRef<SVerticalBox> Equipment = SNew(SVerticalBox);
	TArray<ULootingWidgetPart*> Cards;
	TArray<ULootingWidgetPart*> EquipmentViews;
	const FName Definitions[] = {TEXT("AK"), TEXT("SCARH")};
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FExtractionItem Item = ExtractionItems::Make(Definitions[Index], 1);
		auto* Equip = CreateWidget<ULootingWidgetPart>(World, EquipBP->GeneratedClass.Get());
		Equip->Label->SetText(FText::FromString(Index == 0 ? TEXT("手持武器") : TEXT("背负武器")));
		EquipmentViews.Add(Equip);
		auto* Card = CreateWidget<ULootingWidgetPart>(World, ItemBP->GeneratedClass.Get());
		Card->SetItem(Item);
		Fill(Equip->ItemCanvas, Card);
		Cards.Add(Card);
		Equipment->AddSlot().AutoHeight().Padding(0, 0, 0, 12)[Equip->TakeWidget()];
		auto* GridCard = CreateWidget<ULootingWidgetPart>(World, ItemBP->GeneratedClass.Get());
		GridCard->SetItem(Item);
		At(Container->ItemCanvas, GridCard, FVector2D(Index * 256, 0), FVector2D(254, 126));
		Cards.Add(GridCard);
	}
	FAssetCompilingManager::Get().FinishAllCompilation();
	for (auto* Card : Cards)
		if (auto* Texture = Cast<UTexture2D>(Card->Icon->GetBrush().GetResourceObject()))
		{
			Texture->SetForceMipLevelsToBeResident(30.f);
			Texture->WaitForStreaming();
		}
	auto Preview = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(12)
		[
			SNew(SBox).WidthOverride(430)
			[
				Equipment
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1).Padding(12)
		[
			Container->TakeWidget()
		];
	FWidgetRenderer Renderer(true);
	UTextureRenderTarget2D* Target = Renderer.DrawWidget(Preview, FVector2D(1040, 410));
	FlushRenderingCommands();
	if (!Target) return TEXT("FAILED: no render target");
	for (auto* Card : Cards)
	{
		const FVector2D Size = Card->Icon->GetCachedGeometry().GetLocalSize();
		if (Size.X <= 0 || Size.Y <= 0 || !Card->Icon->GetBrush().GetResourceObject())
			return TEXT("FAILED: weapon image has no arranged area or texture");
	}
	for (auto* Equip : EquipmentViews)
		if (Equip->ItemCanvas->GetCachedGeometry().GetLocalSize().Y < 60)
			return TEXT("FAILED: equipment canvas too small");
	FBufferArchive PNG;
	const FString Output = FPaths::ProjectSavedDir() / TEXT("InventoryUMG/WeaponLayout.png");
	if (!FImageUtils::ExportRenderTarget2DAsPNG(Target, PNG) || !FFileHelper::SaveArrayToFile(PNG, *Output))
		return TEXT("FAILED: preview export");
	return TEXT("PASSED: both weapon slots and backpack cards rendered: ") + Output;
}
