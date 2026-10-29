#include "ExtractionBlueprintBuilder.h"
#include "BrokenEdge/Extraction/ExtractionWidget.h"
#include "BrokenEdge/Extraction/ExtractionLootActor.h"
#include "SessionWidgetStyle.h"
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
	void BuildLayout(UWidgetTree* WidgetTree)
	{
		UBorder* SmokeHaze = nullptr;
		UTextBlock* Status = nullptr;
		UTextBlock* Vitals = nullptr;
		UTextBlock* WeaponStatus = nullptr;
		UTextBlock* ExitStatus = nullptr;
		UTextBlock* Compass = nullptr;
		UTextBlock* ExtractionCountdown = nullptr;
		UProgressBar* ExtractionProgress = nullptr;
		UProgressBar* HealthProgress = nullptr;
		UButton* Previous = nullptr;
		UButton* Next = nullptr;
		UButton* Leave = nullptr;
		auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;
		SmokeHaze = WidgetTree->ConstructWidget<UBorder>();
		// 起始全透明，由 Refresh 按相机距烟心的深度逐帧提升，进烟时不再突然糊满屏。
		SmokeHaze->SetBrushColor(FLinearColor(.34f, .34f, .34f, 0.f));
		SmokeHaze->SetVisibility(ESlateVisibility::Collapsed);
		auto* HazeSlot = Canvas->AddChildToCanvas(SmokeHaze);
		HazeSlot->SetAnchors(FAnchors(0, 0, 1, 1));
		HazeSlot->SetOffsets(FMargin(0));
		auto* Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetBrushColor(FLinearColor(0.015f, 0.025f, 0.04f, .85f));
		Panel->SetPadding(FMargin(14));
		auto* PanelSlot = Canvas->AddChildToCanvas(Panel);
		PanelSlot->SetPosition(FVector2D(18, 18));
		PanelSlot->SetAutoSize(true);
		auto* Box = WidgetTree->ConstructWidget<UVerticalBox>();
		Panel->AddChild(Box);
		Status = SessionWidgetStyle::Text(WidgetTree, TEXT("正在加载航天基地…"), 16);
		Status->SetAutoWrapText(true);
		Status->SetWrapTextAt(400);
		Status->SetMinDesiredWidth(400);
		SessionWidgetStyle::Line(Box, Status, 8);
		Previous = SessionWidgetStyle::Button(WidgetTree, TEXT("Previous"), TEXT("上一位"));
		Next = SessionWidgetStyle::Button(WidgetTree, TEXT("Next"), TEXT("下一位"));
		Leave = SessionWidgetStyle::Button(WidgetTree, TEXT("Leave"), TEXT("离开对局 · 返回房间列表"));
		SessionWidgetStyle::Line(Box, Previous, 5);
		SessionWidgetStyle::Line(Box, Next, 5);
		SessionWidgetStyle::Line(Box, Leave, 0);
		Vitals = SessionWidgetStyle::Text(WidgetTree, TEXT(""), 20);
		auto* VitalsSlot = Canvas->AddChildToCanvas(Vitals);
		VitalsSlot->SetAnchors(FAnchors(0, 1));
		VitalsSlot->SetAlignment(FVector2D(0, 1));
		VitalsSlot->SetPosition(FVector2D(24, -24));
		VitalsSlot->SetSize(FVector2D(440, 100));
		const auto AddText = [&](UTextBlock*& Target, FAnchors Anchor, FVector2D Alignment,
		                         FVector2D Position, FVector2D Size, int32 FontSize)
		{
			Target = SessionWidgetStyle::Text(WidgetTree, TEXT(""), FontSize);
			Target->SetAutoWrapText(true);
			Target->SetVisibility(ESlateVisibility::HitTestInvisible);
			auto* Slot = Canvas->AddChildToCanvas(Target);
			Slot->SetAnchors(Anchor);
			Slot->SetAlignment(Alignment);
			Slot->SetPosition(Position);
			Slot->SetSize(Size);
		};
		AddText(WeaponStatus, FAnchors(1, 1), FVector2D(1, 1), FVector2D(-28, -48), FVector2D(380, 100), 24);
		WeaponStatus->SetJustification(ETextJustify::Right);
		AddText(ExitStatus, FAnchors(1, 0), FVector2D(1, 0), FVector2D(-28, 72), FVector2D(350, 260), 17);
		ExitStatus->SetJustification(ETextJustify::Right);
		ExitStatus->SetColorAndOpacity(FSlateColor(FLinearColor(.42f, .95f, .77f)));
		AddText(Compass, FAnchors(.5f, 0), FVector2D(.5f, 0), FVector2D(0, 22), FVector2D(360, 36), 20);
		Compass->SetJustification(ETextJustify::Center);
		AddText(ExtractionCountdown, FAnchors(.5f, .75f), FVector2D(.5f, 0), FVector2D(0, 0), FVector2D(450, 70), 22);
		ExtractionCountdown->SetJustification(ETextJustify::Center);
		const auto AddBar = [&](UProgressBar*& Target, FAnchors Anchor, FVector2D Alignment,
		                        FVector2D Position, FVector2D Size)
		{
			Target = WidgetTree->ConstructWidget<UProgressBar>();
			Target->SetFillColorAndOpacity(FLinearColor(.25f, .9f, .68f));
			Target->SetVisibility(ESlateVisibility::HitTestInvisible);
			auto* Slot = Canvas->AddChildToCanvas(Target);
			Slot->SetAnchors(Anchor);
			Slot->SetAlignment(Alignment);
			Slot->SetPosition(Position);
			Slot->SetSize(Size);
		};
		AddBar(HealthProgress, FAnchors(0, 1), FVector2D(0, 1), FVector2D(24, -132), FVector2D(280, 6));
		AddBar(ExtractionProgress, FAnchors(.5f, .75f), FVector2D(.5f, 0), FVector2D(0, 74), FVector2D(360, 8));
		SmokeHaze->Rename(TEXT("SmokeHaze"), WidgetTree);
		Status->Rename(TEXT("Status"), WidgetTree);
		Vitals->Rename(TEXT("Vitals"), WidgetTree);
		WeaponStatus->Rename(TEXT("WeaponStatus"), WidgetTree);
		ExitStatus->Rename(TEXT("ExitStatus"), WidgetTree);
		Compass->Rename(TEXT("Compass"), WidgetTree);
		ExtractionCountdown->Rename(TEXT("ExtractionCountdown"), WidgetTree);
		ExtractionProgress->Rename(TEXT("ExtractionProgress"), WidgetTree);
		HealthProgress->Rename(TEXT("HealthProgress"), WidgetTree);

		Previous->SetVisibility(ESlateVisibility::Collapsed);
		Next->SetVisibility(ESlateVisibility::Collapsed);
		Leave->SetVisibility(ESlateVisibility::Collapsed);
		HealthProgress->SetVisibility(ESlateVisibility::Collapsed);
		ExtractionProgress->SetVisibility(ESlateVisibility::Collapsed);
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
		BuildLayout(HUD->WidgetTree);
		FAssetRegistryModule::AssetCreated(HUD);
		HUD->MarkPackageDirty();
		if (!Save(HUD)) return TEXT("FAILED saving HUD");
	}
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
