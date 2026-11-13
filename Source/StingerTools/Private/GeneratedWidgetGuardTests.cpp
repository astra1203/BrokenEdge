#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "GeneratedWidgetGuard.h"
#include "InventoryWidgetBuilder.h"
#include "ExtractionBlueprintBuilder.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeneratedWidgetGuardTest, "BrokenEdge.Extraction.GeneratedWidgets.OverwriteProtection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGeneratedWidgetGuardTest::RunTest(const FString&)
{
    auto* Fixture = NewObject<UWidgetBlueprint>();
    Fixture->ParentClass = UUserWidget::StaticClass();
    Fixture->WidgetTree = NewObject<UWidgetTree>(Fixture);
    auto* Canvas = Fixture->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
    Fixture->WidgetTree->RootWidget = Canvas;
    auto* Label = Fixture->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AuthoredTitle"));
    Label->SetText(FText::FromString(TEXT("Original")));
    auto* Slot = Canvas->AddChildToCanvas(Label);
    const auto Baseline = GeneratedWidgetGuard::Snapshot(Fixture);
    TestTrue(TEXT("Unchanged tree matches"), GeneratedWidgetGuard::Difference(Baseline, GeneratedWidgetGuard::Snapshot(Fixture)).IsEmpty());
    Label->SetText(FText::FromString(TEXT("Designer edited title")));
    FString Difference = GeneratedWidgetGuard::Difference(Baseline, GeneratedWidgetGuard::Snapshot(Fixture));
    TestTrue(TEXT("Manual text edit names the affected widget/property"), Difference.Contains(TEXT("Widget/AuthoredTitle/Text")));
    Label->SetText(FText::FromString(TEXT("Original")));
    Slot->SetPosition(FVector2D(37, 42));
    Difference = GeneratedWidgetGuard::Difference(Baseline, GeneratedWidgetGuard::Snapshot(Fixture));
    TestTrue(TEXT("Layout edits are protected"), Difference.Contains(TEXT("Widget/AuthoredTitle/Slot/LayoutData")));
    Slot->SetPosition(FVector2D::ZeroVector);
    Canvas->RemoveChild(Label);
    TestTrue(TEXT("Removed widgets are identified"), GeneratedWidgetGuard::Difference(Baseline,
        GeneratedWidgetGuard::Snapshot(Fixture)).Contains(TEXT("removed Widget/AuthoredTitle")));

    // Existing production assets have no trusted generation baseline yet. The
    // first default rebuild must refuse before modifying even the first tree.
    for (const TCHAR* Path : {TEXT("/Game/UI/Looting/WBP_LootingSlotView"), TEXT("/Game/UI/Extraction/WBP_ExtractionHUD")})
    {
        auto* BP = LoadObject<UWidgetBlueprint>(nullptr, Path);
        if (!TestNotNull(Path, BP)) return false;
        const auto Before = GeneratedWidgetGuard::Snapshot(BP);
        const bool WasDirty = BP->GetOutermost()->IsDirty();
        const FString Check = GeneratedWidgetGuard::Preflight({FString(Path)});
        if (Check.Contains(TEXT("missing/invalid generation baseline")))
        {
            const FString Result = FString(Path).Contains(TEXT("Looting"))
                ? UInventoryWidgetBuilder::CreateInventoryWidgets() : UExtractionBlueprintBuilder::CreateExtractionBlueprints();
            TestTrue(TEXT("First rebuild refuses untracked existing asset"), Result.StartsWith(TEXT("FAILED: generated widget overwrite refused")));
            TestTrue(TEXT("Refusal preserves tree"), GeneratedWidgetGuard::Difference(Before, GeneratedWidgetGuard::Snapshot(BP)).IsEmpty());
            TestEqual(TEXT("Refusal preserves dirty state"), BP->GetOutermost()->IsDirty(), WasDirty);
        }
    }
    // Round-trip the baseline format, then prove that an edit is refused. Only
    // the sidecar and an in-memory label change; restore both even on failure.
    auto* HUD = LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/UI/Extraction/WBP_ExtractionHUD"));
    if (!HUD) return false;
    const FString Sidecar = FPaths::ProjectConfigDir() / TEXT("GeneratedWidgets/UI/Extraction/WBP_ExtractionHUD.json");
    TArray<uint8> PreviousBytes;
    const bool Existed = IFileManager::Get().FileExists(*Sidecar);
    if (Existed && !FFileHelper::LoadFileToArray(PreviousBytes, *Sidecar)) return false;
    ON_SCOPE_EXIT
    {
        if (Existed) FFileHelper::SaveArrayToFile(PreviousBytes, *Sidecar);
        else IFileManager::Get().Delete(*Sidecar);
    };
    if (!TestTrue(TEXT("Baseline serializes"), GeneratedWidgetGuard::Record(HUD))) return false;
    TestTrue(TEXT("Serialized baseline round-trip accepts unchanged tree"), GeneratedWidgetGuard::Preflight(
        {TEXT("/Game/UI/Extraction/WBP_ExtractionHUD")}).IsEmpty());
    TArray<UWidget*> Widgets;
    HUD->WidgetTree->GetAllWidgets(Widgets);
    UTextBlock* Title = nullptr;
    for (auto* Widget : Widgets) if ((Title = Cast<UTextBlock>(Widget))) break;
    if (!TestNotNull(TEXT("HUD contains a text field"), Title)) return false;
    const FText OldText = Title->GetText();
    ON_SCOPE_EXIT { Title->SetText(OldText); };
    Title->SetText(FText::FromString(TEXT("Automation manual edit fixture")));
    const FString Refusal = GeneratedWidgetGuard::Preflight({TEXT("/Game/UI/Extraction/WBP_ExtractionHUD")});
    TestTrue(TEXT("Saved baseline rejects a later manual edit"), Refusal.StartsWith(TEXT("FAILED:")) && Refusal.Contains(Title->GetName()));
    TestTrue(TEXT("Builder preserves the changed widget when refusing"),
        UExtractionBlueprintBuilder::CreateExtractionBlueprints().StartsWith(TEXT("FAILED: generated widget overwrite refused")));
    TestEqual(TEXT("Refused generator leaves manual text intact"), Title->GetText().ToString(), FString(TEXT("Automation manual edit fixture")));
    return true;
}
#endif
