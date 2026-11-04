#include "ExtractionBlueprintBuilder.h"
#include "BrokenEdge/Extraction/ExtractionWidget.h"
#include "BrokenEdge/Extraction/ExtractionLootActor.h"
#include "BrokenEdge/Extraction/ExtractionUIStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "ActorFactories/ActorFactoryBlueprint.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "BrokenEdge/Extraction/ExtractionGameMode.h"
#include "BrokenEdge/Extraction/ExtractionPlayerController.h"
#include "UObject/UnrealType.h"

namespace
{
	// 局内 HUD 与背包同一套项目自有风格：石墨圆角卡片、细描边、琥珀强调；四角各一张卡片，中间留给准星。
	const FLinearColor Card = ExtractionUIStyle::Hex(TEXT("0B0E12"), .72f);
	const FLinearColor Line = FLinearColor(1, 1, 1, .08f);

	template <class T>
	T* W(UWidgetTree* Tree, FName Name = NAME_None) { return Tree->ConstructWidget<T>(T::StaticClass(), Name); }

	UTextBlock* Label(UWidgetTree* T, FName Name, const FString& Value, int32 Size,
	                  FLinearColor Color = FLinearColor::White, bool bBold = false)
	{
		auto* X = W<UTextBlock>(T, Name);
		X->SetText(FText::FromString(Value));
		X->SetFont(ExtractionUIStyle::Font(Size, bBold));
		X->SetColorAndOpacity(Color);
		X->SetShadowOffset(FVector2D(1, 1));
		X->SetShadowColorAndOpacity(FLinearColor(0, 0, 0, .55f));
		X->SetVisibility(ESlateVisibility::HitTestInvisible);
		return X;
	}

	UBorder* Panel(UWidgetTree* T, FName Name, FLinearColor Color, float Radius, FMargin Padding)
	{
		auto* B = W<UBorder>(T, Name);
		B->SetBrush(ExtractionUIStyle::Panel(FLinearColor::White, Radius, Line));
		B->SetBrushColor(Color);
		B->SetPadding(Padding);
		return B;
	}

	UProgressBar* Bar(UWidgetTree* T, FName Name, FLinearColor Fill)
	{
		auto* P = W<UProgressBar>(T, Name);
		FProgressBarStyle Style;
		Style.SetBackgroundImage(ExtractionUIStyle::Panel(FLinearColor(1, 1, 1, .1f), 2.f));
		Style.SetFillImage(ExtractionUIStyle::Panel(FLinearColor::White, 2.f));
		P->SetWidgetStyle(Style);
		P->SetFillColorAndOpacity(Fill);
		P->SetVisibility(ESlateVisibility::HitTestInvisible);
		return P;
	}

	UWidget* Sized(UWidgetTree* T, UWidget* Content, float Width, float Height)
	{
		auto* S = W<USizeBox>(T);
		if (Width > 0) S->SetWidthOverride(Width);
		if (Height > 0) S->SetHeightOverride(Height);
		S->SetContent(Content);
		return S;
	}

	// 卡片按锚点贴在屏幕四角或中线上，内容自适应大小。
	UVerticalBox* CardAt(UWidgetTree* T, UCanvasPanel* Canvas, FName Name, FAnchors Anchor, FVector2D Alignment,
	                     FVector2D Position)
	{
		auto* B = Panel(T, Name, Card, 6.f, FMargin(16, 12));
		auto* Slot = Canvas->AddChildToCanvas(B);
		Slot->SetAnchors(Anchor);
		Slot->SetAlignment(Alignment);
		Slot->SetPosition(Position);
		Slot->SetAutoSize(true);
		auto* Box = W<UVerticalBox>(T);
		B->SetContent(Box);
		return Box;
	}

	// 卡片标题：左侧强调竖条 + 小字。
	UHorizontalBox* Heading(UWidgetTree* T, UWidget* Title)
	{
		auto* Row = W<UHorizontalBox>(T);
		auto* Accent = Panel(T, NAME_None, ExtractionUIStyle::Accent(), 1.f, FMargin(0));
		auto* AccentSlot = Row->AddChildToHorizontalBox(Sized(T, Accent, 3, 0));
		AccentSlot->SetPadding(FMargin(0, 3, 8, 3));
		Row->AddChildToHorizontalBox(Title)->SetVerticalAlignment(VAlign_Center);
		return Row;
	}

	void BuildLayout(UWidgetTree* WidgetTree)
	{
		auto* T = WidgetTree;
		const FLinearColor Muted = ExtractionUIStyle::Muted();
		auto* Canvas = W<UCanvasPanel>(T);
		T->RootWidget = Canvas;
		auto* SmokeHaze = W<UBorder>(T, TEXT("SmokeHaze"));
		// 起始全透明，由 Refresh 按相机距烟心的深度逐帧提升，进烟时不再突然糊满屏。
		SmokeHaze->SetBrushColor(FLinearColor(.34f, .34f, .34f, 0.f));
		SmokeHaze->SetVisibility(ESlateVisibility::Collapsed);
		auto* HazeSlot = Canvas->AddChildToCanvas(SmokeHaze);
		HazeSlot->SetAnchors(FAnchors(0, 0, 1, 1));
		HazeSlot->SetOffsets(FMargin(0));

		// 左上角留给小地图（UExtractionWidget::PaintMinimap 直接绘制，边长 MiniSize）；
		// 其下：对局时间、地图与存活人数、任务与事件文本、观战/离开按钮。
		auto* Raid = CardAt(T, Canvas, TEXT("RaidCard"), FAnchors(0, 0), FVector2D(0, 0),
		                    FVector2D(24, 24 + UExtractionWidget::MiniSize + 14));
		Raid->AddChildToVerticalBox(Heading(T, Label(T, TEXT("RaidTimer"), TEXT("--:--"), 28, FLinearColor::White, true)));
		Raid->AddChildToVerticalBox(Label(T, TEXT("RaidInfo"), TEXT("航天基地"), 13, Muted))->SetPadding(FMargin(11, 0, 0, 8));
		auto* Status = Label(T, TEXT("Status"), TEXT("正在加载航天基地…"), 14, FLinearColor(1, 1, 1, .9f));
		Status->SetAutoWrapText(true);
		Status->SetWrapTextAt(330);
		Status->SetMinDesiredWidth(330);
		Raid->AddChildToVerticalBox(Status)->SetPadding(FMargin(0, 0, 0, 4));
		for (const auto& [Name, Text] : {
			     TPair<const TCHAR*, const TCHAR*>(TEXT("Previous"), TEXT("上一位")),
			     TPair<const TCHAR*, const TCHAR*>(TEXT("Next"), TEXT("下一位")),
			     TPair<const TCHAR*, const TCHAR*>(TEXT("Leave"), TEXT("离开对局 · 返回房间列表"))
		     })
		{
			auto* Button = W<UButton>(T, Name);
			FButtonStyle Style;
			Style.SetNormal(ExtractionUIStyle::Panel(FLinearColor(1, 1, 1, .06f), 3.f, Line));
			Style.SetHovered(ExtractionUIStyle::Panel(ExtractionUIStyle::Accent(.25f), 3.f, Line));
			Style.SetPressed(ExtractionUIStyle::Panel(ExtractionUIStyle::Accent(.4f), 3.f, Line));
			Button->SetStyle(Style);
			Button->SetContent(Label(T, NAME_None, Text, 14));
			Button->SetVisibility(ESlateVisibility::Collapsed);
			Raid->AddChildToVerticalBox(Sized(T, Button, 0, 34))->SetPadding(FMargin(0, 6, 0, 0));
		}

		// 顶部中央：指南针。
		auto* CompassBox = CardAt(T, Canvas, NAME_None, FAnchors(.5f, 0), FVector2D(.5f, 0), FVector2D(0, 20));
		auto* Compass = Label(T, TEXT("Compass"), TEXT(""), 17, FLinearColor::White, true);
		Compass->SetJustification(ETextJustify::Center);
		CompassBox->AddChildToVerticalBox(Sized(T, Compass, 260, 0));

		// 右上：撤离点列表。
		auto* Exits = CardAt(T, Canvas, NAME_None, FAnchors(1, 0), FVector2D(1, 0), FVector2D(-24, 24));
		Exits->AddChildToVerticalBox(Heading(T, Label(T, NAME_None, TEXT("撤离点"), 14, Muted, true)))->SetPadding(
			FMargin(0, 0, 0, 6));
		auto* ExitStatus = Label(T, TEXT("ExitStatus"), TEXT(""), 14, ExtractionUIStyle::Hex(TEXT("CFE8D6")));
		ExitStatus->SetAutoWrapText(true);
		ExitStatus->SetWrapTextAt(280);
		ExitStatus->SetMinDesiredWidth(280);
		Exits->AddChildToVerticalBox(ExitStatus);

		// 右侧中间：按键提示与技能冷却。
		auto* Hints = CardAt(T, Canvas, TEXT("HintCard"), FAnchors(1, .5f), FVector2D(1, .5f), FVector2D(-24, 0));
		Hints->AddChildToVerticalBox(Heading(T, Label(T, NAME_None, TEXT("操作"), 13, Muted, true)))->SetPadding(
			FMargin(0, 0, 0, 6));
		auto* KeyHints = Label(T, TEXT("KeyHints"), TEXT(""), 13, FLinearColor(1, 1, 1, .82f));
		KeyHints->SetMinDesiredWidth(200);
		Hints->AddChildToVerticalBox(KeyHints);

		// 中下：撤离倒计时，只在撤离中或被阻止时出现。
		auto* Extract = CardAt(T, Canvas, TEXT("ExtractionCard"), FAnchors(.5f, .72f), FVector2D(.5f, 0), FVector2D(0, 0));
		auto* Countdown = Label(T, TEXT("ExtractionCountdown"), TEXT(""), 18, FLinearColor::White, true);
		Countdown->SetJustification(ETextJustify::Center);
		Countdown->SetAutoWrapText(true);
		Extract->AddChildToVerticalBox(Sized(T, Countdown, 380, 0));
		Extract->AddChildToVerticalBox(Sized(T, Bar(T, TEXT("ExtractionProgress"), ExtractionUIStyle::Accent()), 380, 6))->
		         SetPadding(FMargin(0, 10, 0, 2));
		Cast<UWidget>(Extract->GetParent())->SetVisibility(ESlateVisibility::Collapsed);

		// 左下：生命、生命条、护甲与伤情。
		auto* Vitals = CardAt(T, Canvas, NAME_None, FAnchors(0, 1), FVector2D(0, 1), FVector2D(24, -24));
		auto* HealthRow = W<UHorizontalBox>(T);
		HealthRow->AddChildToHorizontalBox(Label(T, TEXT("HealthText"), TEXT("100"), 32, FLinearColor::White, true));
		HealthRow->AddChildToHorizontalBox(Label(T, NAME_None, TEXT("生命"), 13, Muted))->SetVerticalAlignment(VAlign_Bottom);
		Cast<UHorizontalBoxSlot>(HealthRow->GetChildAt(1)->Slot)->SetPadding(FMargin(8, 0, 0, 7));
		Vitals->AddChildToVerticalBox(HealthRow);
		Vitals->AddChildToVerticalBox(Sized(T, Bar(T, TEXT("HealthProgress"), ExtractionUIStyle::Hex(TEXT("5DBB72"))), 300, 6))
		      ->SetPadding(FMargin(0, 2, 0, 8));
		auto* VitalText = Label(T, TEXT("Vitals"), TEXT(""), 13, FLinearColor(1, 1, 1, .85f));
		VitalText->SetAutoWrapText(true);
		VitalText->SetWrapTextAt(300);
		Vitals->AddChildToVerticalBox(VitalText);

		// 右下：武器名、弹匣大字、备弹与弹种。
		auto* Weapon = CardAt(T, Canvas, NAME_None, FAnchors(1, 1), FVector2D(1, 1), FVector2D(-24, -24));
		auto* Name = Label(T, TEXT("WeaponName"), TEXT(""), 15, Muted, true);
		Name->SetJustification(ETextJustify::Right);
		Weapon->AddChildToVerticalBox(Name)->SetHorizontalAlignment(HAlign_Right);
		auto* AmmoRow = W<UHorizontalBox>(T);
		AmmoRow->AddChildToHorizontalBox(Label(T, TEXT("AmmoText"), TEXT(""), 40, FLinearColor::White, true));
		auto* Detail = Label(T, TEXT("WeaponStatus"), TEXT(""), 13, Muted);
		auto* DetailSlot = AmmoRow->AddChildToHorizontalBox(Detail);
		DetailSlot->SetVerticalAlignment(VAlign_Bottom);
		DetailSlot->SetPadding(FMargin(10, 0, 0, 8));
		Weapon->AddChildToVerticalBox(AmmoRow)->SetHorizontalAlignment(HAlign_Right);
	}

	bool Save(UBlueprint* BP)
	{
		FKismetEditorUtilities::CompileBlueprint(BP);
		if (BP->Status == BS_Error) return false;
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		const FString Filename = FPackageName::LongPackageNameToFilename(
			BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		return UPackage::SavePackage(BP->GetOutermost(), BP, *Filename, Args);
	}
}

FString UExtractionBlueprintBuilder::CreateExtractionBlueprints()
{
	const TCHAR* HUDPath = TEXT("/Game/UI/Extraction/WBP_ExtractionHUD");
	auto* HUD = LoadObject<UWidgetBlueprint>(nullptr, HUDPath);
	if (!HUD)
	{
		HUD = CastChecked<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UExtractionWidget::StaticClass(), CreatePackage(HUDPath), TEXT("WBP_ExtractionHUD"), BPTYPE_Normal,
			UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
		FAssetRegistryModule::AssetCreated(HUD);
	}
	else
	{
		// 资产路径不变，控件树整体换成新布局。
		TArray<UWidget*> Old;
		HUD->WidgetTree->GetAllWidgets(Old);
		for (UWidget* X : Old)
		{
			X->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			X->MarkAsGarbage();
		}
		HUD->WidgetTree->RootWidget = nullptr;
	}
	BuildLayout(HUD->WidgetTree);
	HUD->WidgetVariableNameToGuidMap.Reset();
	HUD->WidgetTree->ForEachWidget([HUD](UWidget* X) { HUD->OnVariableAdded(X->GetFName()); });
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(HUD);
	HUD->MarkPackageDirty();
	if (!Save(HUD)) return TEXT("FAILED saving HUD");
	if (HUD->ParentClass != UExtractionWidget::StaticClass()) return TEXT("FAILED HUD parent");
	FKismetEditorUtilities::CompileBlueprint(HUD);
	if (HUD->Status == BS_Error) return TEXT("FAILED HUD compilation");
	const TCHAR* LootPath = TEXT("/Game/Blueprints/Extraction/BP_ExtractionLootActor");
	auto* Loot = LoadObject<UBlueprint>(nullptr, LootPath);
	if (!Loot)
	{
		Loot = FKismetEditorUtilities::CreateBlueprint(AExtractionLootActor::StaticClass(), CreatePackage(LootPath),
		                                               TEXT("BP_ExtractionLootActor"), BPTYPE_Normal);
		FKismetEditorUtilities::CompileBlueprint(Loot);
		auto* Defaults = CastChecked<AExtractionLootActor>(Loot->GeneratedClass->GetDefaultObject());
		Defaults->DefaultContainerMesh = LoadObject<UStaticMesh>(
			nullptr, TEXT(
				"/Game/Maps/SpaceCenter/Assets/409e475f1d6c58b5/SM_H_FutureTech_Tools_Box_01/StaticMeshes/SM_H_FutureTech_Tools_Box_01"));
		if (!Defaults->DefaultContainerMesh) return TEXT("FAILED default loot mesh");
		FAssetRegistryModule::AssetCreated(Loot);
		Loot->MarkPackageDirty();
		if (!Save(Loot)) return TEXT("FAILED saving Loot");
	}
	if (Loot->ParentClass != AExtractionLootActor::StaticClass()) return TEXT("FAILED Loot parent");
	FKismetEditorUtilities::CompileBlueprint(Loot);
	return Loot->Status == BS_Error ? TEXT("FAILED Loot compilation") : TEXT("PASSED");
}

FString UExtractionBlueprintBuilder::MigrateLootActors()
{
	auto* World = GEditor->GetEditorWorldContext().World();
	auto* BP = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Blueprints/Extraction/BP_ExtractionLootActor"));
	if (!World || !BP) return TEXT("FAILED missing world or blueprint");
	TArray<AActor*> OldActors;
	TArray<FTransform> Transforms;
	auto Snapshot = [](AActor* Actor)
	{
		FString Result;
		for (const TCHAR* Name : {
			     TEXT("Loose"), TEXT("LooseDefinition"), TEXT("LooseQuantity"), TEXT("Label"), TEXT("Empty"),
			     TEXT("ContainerId"), TEXT("SourcePointId"), TEXT("ContainerAppearance"), TEXT("AccessDoor"),
			     TEXT("Contents"), TEXT("SeedContents"), TEXT("EquipmentTier"), TEXT("Tags")
		     })
		{
			FProperty* Property = FindFProperty<FProperty>(Actor->GetClass(), Name);
			FString Value;
			Property->ExportText_InContainer(0, Value, Actor, nullptr, Actor, PPF_None);
			Result += FString(Name) + TEXT("=") + Value + TEXT("\n");
		}
		return Result;
	};
	TArray<FString> Snapshots;
	for (TActorIterator<AExtractionLootActor> It(World); It; ++It)
		if (It->GetClass() == AExtractionLootActor::StaticClass())
		{
			OldActors.Add(*It);
			Transforms.Add(It->GetActorTransform());
			Snapshots.Add(Snapshot(*It));
		}
	if (OldActors.IsEmpty()) return TEXT("PASSED 0");
	TArray<AActor*> NewActors;
	UEditorActorSubsystem::ReplaceActors(NewObject<UActorFactoryBlueprint>(), FAssetData(BP), OldActors, &NewActors,
	                                     true);
	if (NewActors.Num() != OldActors.Num()) return TEXT("FAILED incomplete replacement; do not save");
	for (int32 I = 0; I < NewActors.Num(); ++I)
	{
		auto* Loot = CastChecked<AExtractionLootActor>(NewActors[I]);
		Loot->SetActorTransform(Transforms[I]);
		// Property copying may carry the old native default; restore the new asset default.
		if (!Loot->DefaultContainerMesh)
			Loot->DefaultContainerMesh = CastChecked<AExtractionLootActor>(BP->GeneratedClass->GetDefaultObject())->
				DefaultContainerMesh;
		Loot->RerunConstructionScripts();
		if (Snapshot(Loot) != Snapshots[I]) return TEXT("FAILED instance properties changed; do not save");
	}
	return FString::Printf(TEXT("PASSED %d"), NewActors.Num());
}

FString UExtractionBlueprintBuilder::ValidateExtractionBlueprints()
{
	auto* HUDClass = GetDefault<AExtractionPlayerController>()->ExtractionWidgetClass.LoadSynchronous();
	auto* LootClass = GetDefault<AExtractionGameMode>()->LootActorClass.Get();
	if (!HUDClass || !LootClass || !HUDClass->ClassGeneratedBy || !LootClass->ClassGeneratedBy)
		return TEXT("FAILED runtime class configuration");
	auto* World = GEditor->GetEditorWorldContext().World();
	auto* Widget = CreateWidget<UExtractionWidget>(World, HUDClass);
	if (!Widget) return TEXT("FAILED widget instance");
	for (TFieldIterator<FObjectProperty> It(UExtractionWidget::StaticClass()); It; ++It)
		if (It->HasMetaData(TEXT("BindWidget")) && !It->GetObjectPropertyValue_InContainer(Widget))
			return TEXT("FAILED missing binding: ") + It->GetName();
	Widget->Refresh(); // No player/GameState: must safely retain loading defaults.
	auto* Loot = World->SpawnActor<AExtractionLootActor>(LootClass);
	if (!Loot) return TEXT("FAILED loot instance");
	const bool bMesh = Loot->DefaultContainerMesh != nullptr;
	Loot->Destroy();
	return bMesh ? TEXT("PASSED") : TEXT("FAILED loot mesh");
}
