#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "BrokenEdge/Extraction/UI/ExtractionHUDMetrics.h"
#include "BrokenEdge/Extraction/UI/ExtractionWidget.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/Core/ExtractionInteractionComponent.h"
#include "BrokenEdge/Extraction/Core/ExtractionAccountComponent.h"
#include "BrokenEdge/Extraction/World/ExtractionExit.h"
#include "BrokenEdge/Extraction/Core/ExtractionPlayerController.h"
#include "BrokenEdge/Extraction/Core/ExtractionState.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Serialization/JsonSerializer.h"

class FExtractionHUDProfileSequence : public IAutomationLatentCommand
{
public:
    explicit FExtractionHUDProfileSequence(FAutomationTestBase* InTest) : Test(InTest) {}
    ~FExtractionHUDProfileSequence() { if (Capturing) ExtractionHUDMetrics::Finish(); }
    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        if (Now - Started > 110) { Test->AddError(TEXT("HUD capture timed out")); return true; }
        auto* World = GEditor ? GEditor->PlayWorld.Get() : nullptr;
        auto* PC = World ? Cast<AExtractionPlayerController>(World->GetFirstPlayerController()) : nullptr;
        auto* Character = PC ? Cast<ABrokenEdgeCharacter>(PC->GetPawn()) : nullptr;
        auto* State = World ? World->GetGameState<AExtractionGameState>() : nullptr;
        if (!Character || !State || !Character->GetEquippedWeapon()) return false;
        if (!Widget.IsValid())
        {
            TArray<UUserWidget*> Widgets;
            UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, UExtractionWidget::StaticClass(), false);
            for (auto* Candidate : Widgets)
                if (Candidate->GetOwningPlayer() == PC && Candidate->IsInViewport()) Widget = Cast<UExtractionWidget>(Candidate);
            if (!Widget.IsValid()) return false;
            // A repeatable small map with six active exits exercises the raid HUD
            // without requiring the memory-heavy SpaceCenter streaming region.
            State->Phase = EExtractionPhase::Active;
            State->Deadline = State->GetServerWorldTimeSeconds() + 1800;
            for (int32 Index = 0; Index < 6; ++Index)
            {
                auto* Exit = World->SpawnActor<AExtractionExit>();
                FExtractionExitDefinition Definition;
                Definition.Id = FName(*FString::Printf(TEXT("ProfileExit%d"), Index));
                Definition.Label = FText::FromString(Definition.Id.ToString());
                Definition.Transform.SetLocation(Character->GetActorLocation() + FVector(20000 + Index * 5000, 0, 0));
                Exit->Configure(Definition); Exit->StartRound(State->GetServerWorldTimeSeconds());
                Exits.Add(Exit);
            }
            ReadyAt = Now;
            return false;
        }
        if (Now - ReadyAt < 5) return false;
        if (!Capturing)
        {
            Capturing = true; CaptureAt = Now;
            TRACE_BOOKMARK(TEXT("ExtractionHUDProfileBegin"));
            ExtractionHUDMetrics::Begin();
        }
        const double Elapsed = Now - CaptureAt;
        PC->SetControlRotation(FRotator(0, Elapsed * 8, 0));
        if (Elapsed < 60) return false;
        TRACE_BOOKMARK(TEXT("ExtractionHUDProfileEnd"));
        auto Sample = ExtractionHUDMetrics::Finish(); Capturing = false;
        if (!Test->TestTrue(TEXT("HUD refreshed throughout capture"), Sample.RefreshMilliseconds.Num() > 600)) return true;
        double Sum = 0;
        for (double Milliseconds : Sample.RefreshMilliseconds) Sum += Milliseconds;
        Sample.RefreshMilliseconds.Sort();
        auto Report = MakeShared<FJsonObject>();
        Report->SetStringField(TEXT("scenario"), TEXT("WeaponRange standalone PIE; six active exits; rotating view; 5s warmup; 60fps cap"));
        Report->SetNumberField(TEXT("capture_seconds"), Elapsed);
        Report->SetNumberField(TEXT("refresh_calls"), Sample.RefreshMilliseconds.Num());
        Report->SetNumberField(TEXT("mean_ms"), Sum / Sample.RefreshMilliseconds.Num());
        Report->SetNumberField(TEXT("p95_ms"), Sample.RefreshMilliseconds[FMath::Min(Sample.RefreshMilliseconds.Num()-1, FMath::FloorToInt(Sample.RefreshMilliseconds.Num()*.95))]);
        Report->SetNumberField(TEXT("max_ms"), Sample.RefreshMilliseconds.Last());
        Report->SetNumberField(TEXT("set_text_attempts"), Sample.TextAttempts);
        Report->SetNumberField(TEXT("set_text_writes"), Sample.TextWrites);
        FString Json; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
        const FString Output = FPlatformMisc::GetEnvironmentVariable(TEXT("EXTRACTION_RUN_DIR"));
        Test->TestTrue(TEXT("Write bounded capture report"), !Output.IsEmpty() && FFileHelper::SaveStringToFile(Json, *(Output / TEXT("HUDProfile.json"))));
        UE_LOG(LogTemp, Display, TEXT("EXTRACTION_HUD_PROFILE %s"), *Json);
        // Verify lifecycle changes immediately after the measured interval.
        auto* ExitText = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("ExitStatus")));
        if (Test->TestNotNull(TEXT("Exit text binding"), ExitText))
        {
            Widget->Refresh();
            Test->TestTrue(TEXT("Dynamically spawned exit appears"), ExitText->GetText().ToString().Contains(TEXT("ProfileExit0")));
            Exits[0]->Destroy(); Widget->Refresh();
            Test->TestFalse(TEXT("Destroyed exit disappears"), ExitText->GetText().ToString().Contains(TEXT("ProfileExit0")));
            auto* PS = PC->GetPlayerState<AExtractionPlayerState>();
            const auto Life = PS->Life;
            PS->Life = EExtractionLife::Dead; Widget->Refresh();
            Test->TestTrue(TEXT("No stale exit list after leaving alive state"), ExitText->GetText().IsEmpty());
            PS->Life = Life; Widget->Refresh();
            Test->TestTrue(TEXT("Restored alive state refreshes remaining exits"), ExitText->GetText().ToString().Contains(TEXT("ProfileExit1")));
        }
        auto* HealthText = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("HealthText")));
        auto* VitalsText = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("Vitals")));
        auto* WeaponName = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("WeaponName")));
        if (Test->TestNotNull(TEXT("Health binding"), HealthText) && Test->TestNotNull(TEXT("Vitals binding"), VitalsText)
            && Test->TestNotNull(TEXT("Weapon name binding"), WeaponName))
        {
            Character->ApplyVitalsChange({54 - Character->GetHealth(), 0, EExtractionVitalsReason::Restore}); Widget->Refresh();
            Test->TestEqual(TEXT("Changed health is displayed without a stale cache"), HealthText->GetText().ToString(), FString(TEXT("54")));
            auto* Inventory = Character->GetExtractionInteraction()->Inventory();
            Test->TestTrue(TEXT("Prepare equipped armor in training inventory"), Inventory->Mutate([](auto& Candidate, FString&)
            {
                if (!Candidate.InSlot(2))
                {
                    auto Armor = ExtractionItems::Make(TEXT("Helmet2"));
                    Armor.Slot = 2; Candidate.Items.Add(Armor);
                }
                return true;
            }, INDEX_NONE, false).Ok());
            Widget->Refresh();
            const FString BeforeArmor = VitalsText->GetText().ToString();
            Test->TestTrue(TEXT("Change equipped armor through committed inventory"), Inventory->Mutate([](auto& Candidate, FString&)
            {
                auto* Armor = Candidate.Items.FindByPredicate([](const auto& Item) { return Item.Slot == 2; });
                if (!Armor) return false;
                Armor->Durability = FMath::Max(0.f, Armor->Durability - 1.f); return true;
            }, INDEX_NONE, false).Ok());
            Widget->Refresh();
            Test->TestTrue(TEXT("Committed armor change invalidates status text"), VitalsText->GetText().ToString() != BeforeArmor);
            PC->UnPossess(); Widget->Refresh();
            Test->TestEqual(TEXT("Losing pawn clears stale health"), HealthText->GetText().ToString(), FString(TEXT("--")));
            Test->TestTrue(TEXT("Losing pawn clears stale weapon"), WeaponName->GetText().IsEmpty());
            PC->Possess(Character); Widget->Refresh();
            Test->TestFalse(TEXT("Repossessing refreshes weapon"), WeaponName->GetText().IsEmpty());
            Widget->RemoveFromParent(); Widget->AddToViewport(); Widget->Refresh();
            Test->TestEqual(TEXT("Reattached widget refreshes health"), HealthText->GetText().ToString(), FString::FromInt(FMath::CeilToInt(Character->GetHealth())));
        }
        if (auto* Account = PC->ExtractionAccount.Get())
        {
            const auto BeforeProfile = Account->Profile;
            Account->Profile.Progression.Initialize();
            Account->Profile.Progression.FindQuest(TEXT("FirstExtraction"))->Accepted = true;
            ++Account->Profile.Revision; Widget->Refresh();
            auto* StatusText = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("Status")));
            const auto* Rules = GetDefault<UExtractionProgressionRules>();
            if (Test->TestNotNull(TEXT("Status binding"), StatusText))
            {
                Test->TestTrue(TEXT("Profile revision refreshes accepted task"), StatusText->GetText().ToString().Contains(Rules->Quest(TEXT("FirstExtraction"))->Label));
                Account->Profile.Progression.FindQuest(TEXT("FirstExtraction"))->Claimed = true;
                Account->Profile.Progression.FindQuest(TEXT("ClearGuards"))->Accepted = true;
                ++Account->Profile.Revision; Widget->Refresh();
                Test->TestTrue(TEXT("Next revision refreshes next task"), StatusText->GetText().ToString().Contains(Rules->Quest(TEXT("ClearGuards"))->Label));
                Test->TestFalse(TEXT("Claimed task is not left on HUD"), StatusText->GetText().ToString().Contains(Rules->Quest(TEXT("FirstExtraction"))->Label));
            }
            Account->Profile = BeforeProfile; Widget->Refresh();
        }
        return true;
    }
private:
    FAutomationTestBase* Test;
    double Started = FPlatformTime::Seconds(), ReadyAt = 0, CaptureAt = 0;
    bool Capturing = false;
    TWeakObjectPtr<UExtractionWidget> Widget;
    TArray<TWeakObjectPtr<AExtractionExit>> Exits;
};

class FRestoreHUDProfileSettings : public IAutomationLatentCommand
{
public:
    FRestoreHUDProfileSettings()
    {
        const auto* Settings = GetDefault<ULevelEditorPlaySettings>();
        Settings->GetPlayNetMode(Mode); Settings->GetPlayNumberOfClients(Clients);
        Settings->GetRunUnderOneProcess(OneProcess); SeparateServer = Settings->bLaunchSeparateServer;
        MaxFPS = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->GetFloat();
    }
    bool Update() override
    {
        auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
        Settings->SetPlayNetMode(Mode); Settings->SetPlayNumberOfClients(Clients);
        Settings->SetRunUnderOneProcess(OneProcess); Settings->bLaunchSeparateServer = SeparateServer;
        IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->Set(MaxFPS);
        return true;
    }
private:
    EPlayNetMode Mode = PIE_Standalone;
    int32 Clients = 1;
    bool OneProcess = true, SeparateServer = false;
    float MaxFPS = 0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionHUDProfile, "BrokenEdge.Extraction.HUDProfile.SixtySeconds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FExtractionHUDProfile::RunTest(const FString&)
{
    const auto Restore = MakeShared<FRestoreHUDProfileSettings>();
    auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
    Settings->SetPlayNetMode(PIE_Standalone); Settings->SetPlayNumberOfClients(1);
    Settings->SetRunUnderOneProcess(true); Settings->bLaunchSeparateServer = false;
    IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->Set(60.f);
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Extraction/Training/L_WeaponRange")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FExtractionHUDProfileSequence>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
    return true;
}
#endif
