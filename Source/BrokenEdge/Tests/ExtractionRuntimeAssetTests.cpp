#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/CanvasPanel.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Character/BrokenEdgeCharacterPresentation.h"
#include "BrokenEdge/Extraction/Core/ExtractionPlayerController.h"
#include "BrokenEdge/Extraction/Core/ExtractionInteractionComponent.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionItemRegistry.h"
#include "BrokenEdge/Extraction/UI/LootingMainView.h"
#include "BrokenEdge/Extraction/UI/ExtractionWidget.h"

namespace
{
class FRuntimeAssetSequence : public IAutomationLatentCommand
{
public:
    explicit FRuntimeAssetSequence(FAutomationTestBase* InTest) : Test(InTest)
    { FParse::Value(FCommandLine::Get(), TEXT("RuntimeAssetsScreenshot="), Screenshot); }
    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        if (Now - Started > 90)
        { Test->AddError(FString::Printf(TEXT("Runtime asset validation timed out at step %d"), Step)); return true; }
        UWorld* World = nullptr;
        for (const auto& Context : GEngine->GetWorldContexts())
            if (Context.WorldType == EWorldType::Game && Context.World()) { World = Context.World(); break; }
        auto* PC = World ? Cast<AExtractionPlayerController>(World->GetFirstPlayerController()) : nullptr;
        auto* C = PC ? Cast<ABrokenEdgeCharacter>(PC->GetPawn()) : nullptr;
        auto* Interaction = C ? C->GetExtractionInteraction() : nullptr;
        auto* Inventory = Interaction ? Interaction->Inventory() : nullptr;
        if (!C || !C->GetEquippedWeapon() || !Inventory || Inventory->GetContents().Items.IsEmpty()) return false;
        if (!View.IsValid())
        {
            TArray<UUserWidget*> Widgets;
            UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, ULootingMainView::StaticClass(), false);
            for (auto* Widget : Widgets)
                if (Widget->GetOwningPlayer() == PC && Widget->IsInViewport()) View = Cast<ULootingMainView>(Widget);
            if (!View.IsValid()) return false;
        }
        if (Step == 0)
        {
            if (!Interaction->CanAct()) return false;
            if (!Test->TestTrue(TEXT("Runtime smoke uses isolated training map"), World->GetMapName().Contains(TEXT("L_WeaponRange")))) return true;
            const auto* Registry = UExtractionItemRegistry::Get();
            if (!Test->TestNotNull(TEXT("Packaged item registry loads"), Registry)) return true;
            for (const auto& Error : Registry->ValidateEntries()) Test->AddError(Error);
            for (FName Id : UExtractionItemDefinition::Ids())
                Test->TestNotNull(*FString::Printf(TEXT("Runtime definition %s"), *Id.ToString()), UExtractionItemDefinition::Get(Id));
            for (const auto& Error : C->GetPresentation().ValidatePresentation()) Test->AddError(Error);
            Test->TestNotNull(TEXT("Authored inventory input action loads"), PC->FindExtractionInputAction(TEXT("IA_Inventory")));
            for (const auto& Item : Inventory->GetContents().Items)
            {
                const auto* Definition = UExtractionItemDefinition::Get(Item.Definition);
                Test->TestTrue(*FString::Printf(TEXT("Carried item icon %s loads"), *Item.Definition.ToString()),
                    Definition && Definition->Icon.LoadSynchronous() != nullptr);
            }
            TArray<UUserWidget*> HUDs;
            UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, HUDs, UExtractionWidget::StaticClass(), false);
            Test->TestTrue(TEXT("Authored HUD is created in viewport"), HUDs.ContainsByPredicate(
                [PC](UUserWidget* Widget) { return Widget->GetOwningPlayer() == PC && Widget->IsInViewport(); }));
            Test->TestEqual(TEXT("Inventory uses authored widget class"), View->GetClass(), PC->InventoryWidgetClass.LoadSynchronous());
            Revision = Inventory->GetContents().Revision; ItemCount = Inventory->GetContents().Items.Num();
            Test->AddInfo(FString::Printf(TEXT("Runtime assets: registry=%d carried=%d map=%s netmode=%d"),
                Registry->Entries.Num(), ItemCount, *World->GetMapName(), int32(World->GetNetMode())));
            PC->ToggleInventory(); ++Step; StepAt = Now;
        }
        else if (Step == 1)
        {
            if (Now - StepAt < 2 || !Interaction->InventoryOpen || !View->InventoryBody || !View->InventoryBody->IsVisible()) return false;
            Test->TestTrue(TEXT("Inventory builds visible item cells"), View->OwnContainer && View->OwnContainer->ItemCanvas
                && View->OwnContainer->ItemCanvas->GetChildrenCount() > 0);
            if (!Screenshot.IsEmpty()) FScreenshotRequest::RequestScreenshot(Screenshot, true, false);
            ++Step; StepAt = Now;
        }
        else if (Step == 2)
        {
            if (Now - StepAt < 1 || (!Screenshot.IsEmpty() && !FPaths::FileExists(Screenshot) && Now - StepAt < 10)) return false;
            if (!Screenshot.IsEmpty()) Test->TestTrue(TEXT("Rendered inventory capture exists"), FPaths::FileExists(Screenshot));
            PC->ToggleInventory(); ++Step;
        }
        else if (Step == 3)
        {
            if (Interaction->InventoryOpen || View->InventoryBody->IsVisible()) return false;
            Test->TestEqual(TEXT("Opening UI preserves inventory revision"), Inventory->GetContents().Revision, Revision);
            Test->TestEqual(TEXT("Opening UI preserves item instances"), Inventory->GetContents().Items.Num(), ItemCount);
            Test->AddInfo(TEXT("RUNTIME_ASSETS_COMPLETE: definitions, presentation, input, icons, HUD and inventory open/close"));
            return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    double Started = FPlatformTime::Seconds(), StepAt = 0;
    int32 Step = 0, Revision = 0, ItemCount = 0;
    TWeakObjectPtr<ULootingMainView> View;
    FString Screenshot;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionRuntimeAssets, "BrokenEdge.Extraction.RuntimeAssets.TrainingInventory",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FExtractionRuntimeAssets::RunTest(const FString&)
{
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FRuntimeAssetSequence>(this));
    return true;
}
#endif
