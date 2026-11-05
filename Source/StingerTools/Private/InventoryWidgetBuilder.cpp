#include "InventoryWidgetBuilder.h"
#include "BrokenEdge/Extraction/LootingMainView.h"
#include "BrokenEdge/Extraction/ExtractionUIStyle.h"
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
#include "Components/ButtonSlot.h"
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
	// 项目自有界面风格：石墨色圆角面板、细描边、琥珀色强调，不使用任何贴图。
	const FLinearColor Screen = ExtractionUIStyle::Hex(TEXT("07090C"), .9f);
	const FLinearColor Surface = ExtractionUIStyle::Hex(TEXT("0E1115"), .94f);
	const FLinearColor CellColor = ExtractionUIStyle::Hex(TEXT("161A20"), .92f);
	const FLinearColor Line = FLinearColor(1, 1, 1, .07f);

	template <class T>
	T* W(UWidgetTree* Tree, FName Name = NAME_None) { return Tree->ConstructWidget<T>(T::StaticClass(), Name); }

	UTextBlock* Label(UWidgetTree* T, FName Name, const FString& Value, int32 Size = 18,
	                  FLinearColor Color = FLinearColor::White, bool bBold = false)
	{
		auto* X = W<UTextBlock>(T, Name);
		X->SetText(FText::FromString(Value));
		X->SetFont(ExtractionUIStyle::Font(Size, bBold));
		X->SetColorAndOpacity(Color);
		X->SetVisibility(ESlateVisibility::HitTestInvisible);
		return X;
	}

	// 圆角底板：形状与描边放在笔刷里，填充色走 BrushColor，运行时可单独改色（悬停、品质）。
	UBorder* Border(UWidgetTree* T, FName Name, FLinearColor Color, float Radius = 4.f, FLinearColor Outline = Line,
	                FMargin Padding = FMargin(0))
	{
		auto* B = W<UBorder>(T, Name);
		B->SetBrush(ExtractionUIStyle::Panel(FLinearColor::White, Radius, Outline));
		B->SetBrushColor(Color);
		B->SetPadding(Padding);
		return B;
	}

	FButtonStyle ButtonStyle(FLinearColor Normal, FLinearColor Hovered, FLinearColor Pressed)
	{
		FButtonStyle Style;
		Style.SetNormal(ExtractionUIStyle::Panel(Normal, 3.f));
		Style.SetHovered(ExtractionUIStyle::Panel(Hovered, 3.f));
		Style.SetPressed(ExtractionUIStyle::Panel(Pressed, 3.f));
		Style.SetNormalPadding(FMargin(12, 0));
		Style.SetPressedPadding(FMargin(12, 0));
		return Style;
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

	UOverlaySlot* Stack(UOverlay* O, UWidget* X, EHorizontalAlignment H = HAlign_Fill, EVerticalAlignment V = VAlign_Fill,
	                    FMargin Padding = FMargin(0))
	{
		auto* S = O->AddChildToOverlay(X);
		S->SetHorizontalAlignment(H);
		S->SetVerticalAlignment(V);
		S->SetPadding(Padding);
		return S;
	}

	// 新建或就地重建控件蓝图：资产路径不变，引用它的类与关卡不受影响，控件树整体换成新布局。
	UWidgetBlueprint* Create(const TCHAR* Name, UClass* Parent, TFunction<void(UWidgetTree*)> Layout,
	                         TArray<UWidgetBlueprint*>& All)
	{
		const FString Path = Folder + Name;
		auto* BP = LoadObject<UWidgetBlueprint>(nullptr, *(Path + TEXT(".") + Name));
		if (BP)
		{
			TArray<UWidget*> Old;
			BP->WidgetTree->GetAllWidgets(Old);
			for (UWidget* X : Old)
			{
				X->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
				X->MarkAsGarbage();
			}
			BP->WidgetTree->RootWidget = nullptr;
		}
		else
		{
			auto* Package = CreatePackage(*Path);
			BP = CastChecked<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				Parent, Package, FName(Name), BPTYPE_Normal, UWidgetBlueprint::StaticClass(),
				UWidgetBlueprintGeneratedClass::StaticClass()));
			FAssetRegistryModule::AssetCreated(BP);
		}
		Layout(BP->WidgetTree);
		BP->WidgetVariableNameToGuidMap.Reset();
		BP->WidgetTree->ForEachWidget([BP](UWidget* X) { BP->OnVariableAdded(X->GetFName()); });
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
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

	// 一列面板：圆角底板 + 左侧强调竖条 + 标题，返回内容区矩形。
	FBox2D Column(UWidgetTree* T, UCanvasPanel* Body, FVector2D Position, FVector2D Size, UTextBlock* Title)
	{
		At(Body, Border(T, NAME_None, Surface, 6.f), Position, Size);
		At(Body, Border(T, NAME_None, ExtractionUIStyle::Accent(), 1.f, FLinearColor::Transparent),
		   Position + FVector2D(18, 22), FVector2D(4, 20), 1);
		At(Body, Title, Position + FVector2D(32, 17), FVector2D(Size.X - 50, 30), 1);
		return FBox2D(Position + FVector2D(16, 62), Position + Size - FVector2D(16, 16));
	}
}

FString UInventoryWidgetBuilder::CreateInventoryWidgets()
{
	TArray<UWidgetBlueprint*> All;
	const FLinearColor Muted = ExtractionUIStyle::Muted();
	auto* Item = Create(TEXT("WBP_LootingSlotView"), ULootingWidgetPart::StaticClass(), [](UWidgetTree* T)
	{
		auto* Size = W<USizeBox>(T,TEXT("ContentSize"));
		T->RootWidget = Size;
		auto* BG = Border(T,TEXT("Background"), CellColor, 3.f, FLinearColor(1, 1, 1, .06f));
		Size->SetContent(BG);
		auto* O = W<UOverlay>(T);
		BG->SetContent(O);
		auto* Fit = W<UScaleBox>(T,TEXT("WeaponIconFit"));
		Fit->SetStretch(EStretch::ScaleToFit);
		Fit->SetVisibility(ESlateVisibility::HitTestInvisible);
		auto* Image = W<UImage>(T,TEXT("Icon"));
		Image->SetVisibility(ESlateVisibility::HitTestInvisible);
		Fit->SetContent(Image);
		auto* Inner = CastChecked<UScaleBoxSlot>(Image->Slot);
		Inner->SetHorizontalAlignment(HAlign_Center);
		Inner->SetVerticalAlignment(VAlign_Center);
		Stack(O, Fit, HAlign_Fill, VAlign_Fill, FMargin(6, 6, 6, 9));
		auto* Bar = Border(T,TEXT("RarityBar"), ExtractionUIStyle::Rarity(0), 1.f, FLinearColor::Transparent);
		auto* BarSize = W<USizeBox>(T);
		BarSize->SetHeightOverride(3);
		BarSize->SetContent(Bar);
		Stack(O, BarSize, HAlign_Fill, VAlign_Bottom, FMargin(4, 0, 4, 3));
		auto* Title = Label(T,TEXT("Label"),TEXT("?"), 13, ExtractionUIStyle::Muted());
		Title->SetAutoWrapText(true);
		Title->SetJustification(ETextJustify::Center);
		Stack(O, Title, HAlign_Fill, VAlign_Center, FMargin(4));
		Stack(O, Label(T,TEXT("Detail"),TEXT("×1"), 12), HAlign_Right, VAlign_Bottom, FMargin(0, 0, 6, 7));
	}, All);
	auto* Cell = Create(TEXT("WBP_LootingGridCell"), ULootingWidgetPart::StaticClass(), [](UWidgetTree* T)
	{
		T->RootWidget = Border(T,TEXT("Background"), CellColor.CopyWithNewOpacity(.75f), 2.f, FLinearColor(1, 1, 1, .045f));
	}, All);
	auto* Slot = Create(TEXT("WBP_LootEquipSlot"), ULootingWidgetPart::StaticClass(), [Muted](UWidgetTree* T)
	{
		auto* S = W<USizeBox>(T,TEXT("ContentSize"));
		S->SetHeightOverride(100);
		T->RootWidget = S;
		auto* BG = Border(T,TEXT("Background"), ExtractionUIStyle::Hex(TEXT("12151A"), .92f), 4.f, Line, FMargin(8, 6, 8, 8));
		S->SetContent(BG);
		auto* O = W<UOverlay>(T);
		BG->SetContent(O);
		Stack(O, Label(T,TEXT("Label"),TEXT("装备槽"), 12, Muted), HAlign_Left, VAlign_Top);
		Stack(O, W<UCanvasPanel>(T,TEXT("ItemCanvas")), HAlign_Fill, VAlign_Fill, FMargin(0, 20, 0, 0));
	}, All);
	auto* Action = Create(TEXT("WBP_LootingMenuAction"), ULootingWidgetPart::StaticClass(), [](UWidgetTree* T)
	{
		auto* S = W<USizeBox>(T);
		S->SetHeightOverride(40);
		T->RootWidget = S;
		auto* B = W<UButton>(T,TEXT("ActionButton"));
		B->SetStyle(ButtonStyle(FLinearColor::Transparent, ExtractionUIStyle::Accent(.18f), ExtractionUIStyle::Accent(.3f)));
		S->SetContent(B);
		auto* Text = Label(T,TEXT("Label"),TEXT("操作"), 15);
		B->SetContent(Text);
		if (auto* BS = Cast<UButtonSlot>(Text->Slot)) BS->SetHorizontalAlignment(HAlign_Left);
	}, All);
	auto* Container = Create(TEXT("WBP_LootingBackpackV2_Pc"), ULootingWidgetPart::StaticClass(), [](UWidgetTree* T)
	{
		auto* BG = Border(T,TEXT("Background"), FLinearColor::Transparent, 0.f, FLinearColor::Transparent);
		T->RootWidget = BG;
		auto* Scroll = W<UScrollBox>(T);
		BG->SetContent(Scroll);
		auto* V = W<UVerticalBox>(T);
		Scroll->AddChild(V);
		V->AddChildToVerticalBox(W<UVerticalBox>(T,TEXT("Entries")))->SetPadding(FMargin(0, 0, 0, 6));
		auto* Size = W<USizeBox>(T,TEXT("ContentSize"));
		Size->SetWidthOverride(384);
		Size->SetHeightOverride(256);
		V->AddChildToVerticalBox(Size)->SetHorizontalAlignment(HAlign_Left);
		auto* O = W<UOverlay>(T);
		Size->SetContent(O);
		auto* Cells = W<UCanvasPanel>(T,TEXT("GridCanvas"));
		Cells->SetVisibility(ESlateVisibility::HitTestInvisible);
		Stack(O, Cells);
		Stack(O, W<UCanvasPanel>(T,TEXT("ItemCanvas")));
	}, All);
	auto* Menu = Create(TEXT("WBP_LootingContextMenu"), ULootingWidgetPart::StaticClass(), [](UWidgetTree* T)
	{
		auto* S = W<USizeBox>(T);
		S->SetWidthOverride(240);
		S->SetMaxDesiredHeight(700);
		T->RootWidget = S;
		auto* B = Border(T,TEXT("Background"), ExtractionUIStyle::Hex(TEXT("101318"), .98f), 5.f,
		                 FLinearColor(1, 1, 1, .1f), FMargin(6));
		S->SetContent(B);
		auto* Scroll = W<UScrollBox>(T);
		B->SetContent(Scroll);
		Scroll->AddChild(W<UVerticalBox>(T,TEXT("Entries")));
	}, All);
	auto* Split = Create(TEXT("WBP_LootingSplitDialog"), ULootingWidgetPart::StaticClass(), [Muted](UWidgetTree* T)
	{
		auto* B = Border(T,TEXT("Background"), ExtractionUIStyle::Hex(TEXT("101318"), .98f), 6.f,
		                 FLinearColor(1, 1, 1, .1f), FMargin(26));
		T->RootWidget = B;
		auto* V = W<UVerticalBox>(T);
		B->SetContent(V);
		V->AddChildToVerticalBox(Label(T,TEXT("Label"),TEXT("拆分数量"), 22, FLinearColor::White, true));
		auto* Spin = W<USpinBox>(T,TEXT("SplitAmount"));
		Spin->SetDelta(1);
		Spin->SetMinValue(1);
		Spin->SetMaxValue(99);
		V->AddChildToVerticalBox(Spin)->SetPadding(FMargin(0, 20));
		auto* Button = W<UButton>(T,TEXT("ActionButton"));
		Button->SetStyle(ButtonStyle(ExtractionUIStyle::Accent(.9f), ExtractionUIStyle::Accent(), ExtractionUIStyle::Accent(.75f)));
		auto* Confirm = Label(T, NAME_None,TEXT("确认并选择目标格子"), 16, ExtractionUIStyle::Hex(TEXT("15110A")), true);
		Button->SetContent(Confirm);
		auto* ButtonSize = W<USizeBox>(T);
		ButtonSize->SetHeightOverride(44);
		ButtonSize->SetContent(Button);
		V->AddChildToVerticalBox(ButtonSize);
		V->AddChildToVerticalBox(Label(T,TEXT("Detail"),TEXT("Enter 确认 · Esc 取消"), 14, Muted))->SetPadding(
			FMargin(0, 14, 0, 0));
	}, All);
	auto* Prompt = Create(TEXT("WBP_LootingInteractionPrompt"), ULootingWidgetPart::StaticClass(), [Muted](UWidgetTree* T)
	{
		auto* V = W<UVerticalBox>(T);
		T->RootWidget = V;
		auto* L = Label(T,TEXT("Label"),TEXT("E 交互"), 20, FLinearColor::White, true);
		L->SetJustification(ETextJustify::Center);
		L->SetShadowOffset(FVector2D(1, 1));
		L->SetShadowColorAndOpacity(FLinearColor(0, 0, 0, .7f));
		V->AddChildToVerticalBox(L);
		auto* P = W<UProgressBar>(T,TEXT("Progress"));
		P->SetFillColorAndOpacity(ExtractionUIStyle::Accent());
		auto* S = W<USizeBox>(T);
		S->SetHeightOverride(4);
		S->SetContent(P);
		V->AddChildToVerticalBox(S)->SetPadding(FMargin(120, 12));
		auto* D = Label(T,TEXT("Detail"),TEXT(""), 16, ExtractionUIStyle::Accent());
		D->SetJustification(ETextJustify::Center);
		V->AddChildToVerticalBox(D);

	}, All);
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
		auto* Back = W<UBorder>(T,TEXT("ScreenBackground"));
		Back->SetBrushColor(Screen);
		Fill(Body, Back);
		// 顶栏：标题、携行价值、关闭提示，下方一条分隔线。
		At(Body, Label(T, NAME_None,TEXT("背包"), 34, FLinearColor::White, true), FVector2D(80, 44), FVector2D(400, 50));
		At(Body, Label(T,TEXT("ValueText"),TEXT(""), 17, Muted), FVector2D(80, 96), FVector2D(1000, 30));
		auto* Close = Label(T, NAME_None,TEXT("Tab / Esc 关闭"), 15, Muted);
		Close->SetJustification(ETextJustify::Right);
		At(Body, Close, FVector2D(1540, 60), FVector2D(300, 30));
		At(Body, Border(T, NAME_None, Line, 0.f, FLinearColor::Transparent), FVector2D(80, 140), FVector2D(1760, 1));
		// 三列：装备、携行网格、战利品/搜索容器。
		const FBox2D Gear = Column(T, Body, FVector2D(80, 160), FVector2D(420, 760),
		                           Label(T, NAME_None,TEXT("装备"), 19, FLinearColor::White, true));
		const FBox2D Own = Column(T, Body, FVector2D(520, 160), FVector2D(552, 760),
		                          Label(T,TEXT("OwnTitle"),TEXT("携行"), 19, FLinearColor::White, true));
		auto* Sort = W<UButton>(T,TEXT("SortButton"));
		Sort->SetStyle(ButtonStyle(FLinearColor(1, 1, 1, .06f), ExtractionUIStyle::Accent(.25f), ExtractionUIStyle::Accent(.4f)));
		Sort->SetContent(Label(T, NAME_None,TEXT("整理  T"), 13));
		At(Body, Sort, FVector2D(Own.Max.X - 96, 176), FVector2D(96, 30), 2);
		const FBox2D Loot = Column(T, Body, FVector2D(1092, 160), FVector2D(748, 760),
		                           Label(T,TEXT("LootTitle"),TEXT("搜索容器以查看物资"), 19, FLinearColor::White, true));
		auto* TakeAll = W<UButton>(T,TEXT("TakeAllButton"));
		TakeAll->SetStyle(ButtonStyle(FLinearColor(1, 1, 1, .06f), ExtractionUIStyle::Accent(.25f), ExtractionUIStyle::Accent(.4f)));
		TakeAll->SetContent(Label(T, NAME_None,TEXT("全部拾取  F"), 13));
		At(Body, TakeAll, FVector2D(Loot.Max.X - 120, 176), FVector2D(120, 30), 2);
		auto* Search = W<UProgressBar>(T,TEXT("SearchProgress"));
		Search->SetFillColorAndOpacity(ExtractionUIStyle::Accent());
		Search->SetVisibility(ESlateVisibility::Hidden);
		At(Body, Search, FVector2D(Loot.Min.X, Loot.Min.Y - 12), FVector2D(Loot.GetSize().X, 3), 1);
		auto* Scroll = W<UScrollBox>(T);
		At(Body, Scroll, Gear.Min, Gear.GetSize(), 1);
		Scroll->AddChild(W<UVerticalBox>(T,TEXT("EquipmentList")));
		At(Body, Part(T, Container,TEXT("OwnContainer")), Own.Min, Own.GetSize(), 1);
		At(Body, Part(T, Container,TEXT("LootContainer")), Loot.Min, Loot.GetSize(), 1);
		// 底栏：操作结果与快捷键说明。
		At(Body, Label(T,TEXT("StatusText"),TEXT(""), 17, ExtractionUIStyle::Accent()), FVector2D(80, 940),
		   FVector2D(1760, 30));
		At(Body, Label(T, NAME_None,TEXT("拖动放置      R 旋转      Shift + 左键 快速转移      右键 更多操作      T 整理背包      F 全部拾取"), 15, Muted),
		   FVector2D(80, 984), FVector2D(1400, 30));
		auto* Popups = W<UCanvasPanel>(T,TEXT("PopupCanvas"));
		Popups->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Fill(Body, Popups, 10);
		auto* Context = Part(T, Menu,TEXT("ContextMenu"));
		auto* CS = Popups->AddChildToCanvas(Context);
		CS->SetAutoSize(true);
		CS->SetZOrder(1);
		Context->SetVisibility(ESlateVisibility::Collapsed);
		auto* Dialog = Part(T, Split,TEXT("SplitDialog"));
		At(Popups, Dialog, FVector2D(730, 370), FVector2D(460, 270), 2);
		Dialog->SetVisibility(ESlateVisibility::Collapsed);
		auto* Hints = Part(T, Prompt,TEXT("InteractionPrompt"));
		Hints->SetVisibility(ESlateVisibility::HitTestInvisible);
		At(Canvas, Hints, FVector2D(460, 820), FVector2D(1000, 230), 20);
	}, All);
	auto* CDO = CastChecked<ULootingMainView>(Root->GeneratedClass->GetDefaultObject());
	CDO->ItemClass = Item->GeneratedClass;
	CDO->CellClass = Cell->GeneratedClass;
	CDO->SlotClass = Slot->GeneratedClass;
	CDO->ActionClass = Action->GeneratedClass;
	for (auto* BP : All) if (!Save(BP)) return TEXT("FAILED: ") + BP->GetPathName();
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
