#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "BrokenEdge/Extraction/ExtractionState.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Extraction/ExtractionPlayerController.h"
#include "BrokenEdge/Extraction/ExtractionInteractionComponent.h"
#include "BrokenEdge/Extraction/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/ExtractionCombatComponent.h"
#include "BrokenEdge/Extraction/ExtractionAbilities.h"
#include "BrokenEdge/Extraction/ExtractionFragGrenade.h"
#include "BrokenEdge/Extraction/LootingMainView.h"

class FExtractionInputPIESequence : public IAutomationLatentCommand
{
public:
    explicit FExtractionInputPIESequence(FAutomationTestBase* InTest) : Test(InTest) {}
    bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 90) { Test->AddError(TEXT("Input PIE timed out")); return true; }
        auto* W = GEditor ? GEditor->PlayWorld.Get() : nullptr;
        auto* PC = W ? Cast<AExtractionPlayerController>(W->GetFirstPlayerController()) : nullptr;
        auto* C = PC ? Cast<ABrokenEdgeCharacter>(PC->GetPawn()) : nullptr;
        if (!C || !PC->Interaction() || !PC->Interaction()->CanAct() || !C->GetEquippedWeapon()) return false;
        const double Now = W->GetTimeSeconds();
        auto Key = [&](FKey K, bool Down)
        { PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, Down ? IE_Pressed : IE_Released, Down ? 1.f : 0.f)); };
        auto Next = [&] { ++Step; At = Now; };
        auto* Ability = C->GetExtractionAbilities();
        auto* Inventory = PC->Interaction()->Inventory();
        if (Step && Now - At < .12) return false;
        switch (Step)
        {
        case 0:
        {
            auto* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
            if (!Subsystem || !PC->LoadedInputContext) return false;
            // Never modify saved IMCs. Replace this controller's runtime context with a transient copy.
            auto* Remapped = DuplicateObject<UInputMappingContext>(PC->LoadedInputContext, PC);
            for (const auto& Pair : TArray<TPair<FName, FKey>>{
                {TEXT("IA_Grenade"), EKeys::F6}, {TEXT("IA_Interact"), EKeys::F7}, {TEXT("IA_Heal"), EKeys::F8},
                {TEXT("IA_HoldBreath"), EKeys::K}})
            {
                const auto* Action = PC->FindExtractionInputAction(Pair.Key);
                if (!Test->TestNotNull(TEXT("Remappable action"), Action)) return true;
                Remapped->UnmapAllKeysFromAction(Action);
                Remapped->MapKey(Action, Pair.Value);
            }
            Subsystem->RemoveMappingContext(PC->LoadedInputContext);
            PC->LoadedInputContext = Remapped;
            Subsystem->AddMappingContext(Remapped, 10);
            auto Item = ExtractionItems::Make(TEXT("FragGrenade"));
            GrenadeId = Item.Id;
            Test->TestTrue(TEXT("Seed throwable through inventory transaction"), Inventory->Mutate(
                [&](FExtractionContainer& Candidate, FString&) { return ExtractionItems::AutoPlace(Candidate, Item); },
                INDEX_NONE, false).Ok());
            Next(); return false;
        }
        case 1: Key(EKeys::F6, true); Next(); return false;
        case 2:
            Test->TestTrue(TEXT("Remapped grenade enters aim"), PC->bAimingGrenade);
            Test->TestNotNull(TEXT("Holding remapped key does not consume grenade"), Inventory->GetContents().Find(GrenadeId));
            Key(EKeys::F6, false); Next(); return false;
        case 3:
            Test->TestFalse(TEXT("Remapped release exits aim"), PC->bAimingGrenade);
            Test->TestNull(TEXT("Remapped release consumes exactly the seeded grenade"), Inventory->GetContents().Find(GrenadeId));
            for (TActorIterator<AExtractionFragGrenade> It(W); It; ++It) if (It->GetOwner() == C) It->Destroy();
            Key(EKeys::F7, true); Next(); return false;
        case 4:
            // Isolate release cancellation from the ability's periodic target validation.
            Ability->SetComponentTickEnabled(false);
            Ability->Action = EExtractionAction::Revive;
            Ability->ActionEnd = Now + 100;
            Key(EKeys::F7, false); Next(); return false;
        case 5:
            Test->TestTrue(TEXT("Remapped interaction release cancels rescue"), Ability->Action == EExtractionAction::None);
            Ability->SetComponentTickEnabled(true);
            if (C->GetCombatState() != ECombatState::ECS_Unoccuiped) return false;
            Key(EKeys::K, true);
            Key(EKeys::F6, true); Next(); return false;
        case 6:
            if (!bReleasedModifier)
            {
                Test->TestTrue(TEXT("Remapped modifier selects low throw"), PC->bAimingGrenadeLow);
                Key(EKeys::K, false);
                bReleasedModifier = true;
                return false;
            }
            Test->TestTrue(TEXT("Releasing modifier alone does not throw"), PC->bAimingGrenade);
            PC->ToggleExtractionMenu();
            Test->TestFalse(TEXT("Opening menu cancels grenade aim"), PC->bAimingGrenade);
            Key(EKeys::F6, false);
            PC->ToggleExtractionMenu();
            Key(EKeys::F8, true); Next(); return false;
        case 7:
            PC->HandleApplicationActivation(false);
            Key(EKeys::F8, false); Next(); return false;
        case 8:
            Test->TestFalse(TEXT("Focus cancellation cannot become a healing tap"), Ability->bStimDrawn);
            Test->TestTrue(TEXT("Focus cancellation leaves no healing action"), Ability->Action == EExtractionAction::None);
            Key(EKeys::F8, true); Next(); return false;
        case 9: Key(EKeys::F8, false); Next(); return false;
        case 10:
            Test->TestTrue(TEXT("Remapped healing tap draws stim"), Ability->bStimDrawn);
            Ability->SetStimDrawn(false);
            Key(EKeys::F8, true); Next(); return false;
        case 11:
            if (Now - At < .5) return false;
            Test->AddInfo(FString::Printf(TEXT("Heal observation: CanStart=%d Action=%d Squad=%d Life=%d Falling=%d ReadyAt=%.3f Now=%.3f"),
                Ability->CanStart(), static_cast<int32>(Ability->Action), C->GetPlayerState<AExtractionPlayerState>()->ExtractionSquad,
                static_cast<int32>(C->GetPlayerState<AExtractionPlayerState>()->Life), C->GetCharacterMovement()->IsFalling(),
                Ability->HealReadyAt, Now));
            Test->TestTrue(TEXT("Hold trigger starts healing without Tick key timing"), PC->bHealHoldUsed);
            Test->TestTrue(TEXT("Hold reaches authoritative ability"), Ability->Action == EExtractionAction::Heal || Ability->HealReadyAt > Now);
            PC->HandleApplicationActivation(false);
            Key(EKeys::F8, false); Next(); return false;
        case 12:
            Key(EKeys::LeftMouseButton, true); Next(); return false;
        case 13:
            PC->ToggleExtractionMenu();
            Test->TestFalse(TEXT("Menu clears held fire"), C->GetCombat()->IsFireInputHeld());
            Shots = C->GetExtractionCombat()->GetAcceptedShots();
            Key(EKeys::LeftMouseButton, false);
            PC->ToggleExtractionMenu(); Next(); return false;
        case 14:
        {
            if (Now - At < .4) return false;
            Test->TestEqual(TEXT("No delayed shot after menu close"), C->GetExtractionCombat()->GetAcceptedShots(), Shots);
            auto* RemappedUI = DuplicateObject<UInputMappingContext>(PC->LoadedUIInputContext, PC);
            const auto* Rotate = PC->FindExtractionInputAction(TEXT("IA_RotateItem"));
            RemappedUI->UnmapAllKeysFromAction(Rotate);
            RemappedUI->MapKey(Rotate, EKeys::Y);
            PC->LoadedUIInputContext = RemappedUI;
            PC->RequestOpenInventory(nullptr, true);
            Next(); return false;
        }
        case 15:
        {
            if (!Test->TestNotNull(TEXT("Live inventory widget"), PC->InventoryWidget.Get())) return true;
            Test->TestTrue(TEXT("Inventory opened for rotation"), PC->Interaction()->InventoryOpen);
            auto* Part = NewObject<ULootingWidgetPart>(PC->InventoryWidget);
            Part->ItemId = Inventory->GetContents().Items[0].Id;
            UDragDropOperation* Operation = nullptr;
            PC->InventoryWidget->BeginDrag(Part, Operation);
            Drag = Cast<ULootingDragDropOperation>(Operation);
            if (!Test->TestNotNull(TEXT("Real inventory drag operation"), Drag.Get())) return true;
            WasRotated = Drag->bRotated;
            Key(EKeys::Y, true); Next(); return false;
        }
        case 16:
            Test->TestTrue(TEXT("Remapped UI action rotates drag exactly once"), Drag.IsValid() && Drag->bRotated != WasRotated);
            Key(EKeys::Y, false); Next(); return false;
        case 17:
            Test->TestTrue(TEXT("Rotation release does not rotate again"), Drag.IsValid() && Drag->bRotated != WasRotated);
            PC->InventoryWidget->CancelDrag();
            PC->RequestOpenInventory(nullptr, false);
            return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    double Started = FPlatformTime::Seconds(), At = 0;
    int32 Step = 0;
    FGuid GrenadeId;
    uint32 Shots = 0;
    bool bReleasedModifier = false;
    TWeakObjectPtr<ULootingDragDropOperation> Drag;
    bool WasRotated = false;
};

class FRestoreExtractionInputPIESettings : public IAutomationLatentCommand
{
public:
    FRestoreExtractionInputPIESettings()
    {
        const auto* Settings = GetDefault<ULevelEditorPlaySettings>();
        Settings->GetPlayNetMode(Mode); Settings->GetPlayNumberOfClients(Clients);
        Settings->GetRunUnderOneProcess(OneProcess); SeparateServer = Settings->bLaunchSeparateServer;
    }
    bool Update() override
    {
        auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
        Settings->SetPlayNetMode(Mode); Settings->SetPlayNumberOfClients(Clients);
        Settings->SetRunUnderOneProcess(OneProcess); Settings->bLaunchSeparateServer = SeparateServer;
        return true;
    }
private:
    EPlayNetMode Mode = PIE_Standalone;
    int32 Clients = 1;
    bool OneProcess = true, SeparateServer = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionInputPIE, "BrokenEdge.Extraction.InputPIE.RemapAndCancellation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FExtractionInputPIE::RunTest(const FString&)
{
    const auto Restore = MakeShared<FRestoreExtractionInputPIESettings>();
    auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
    Settings->SetPlayNetMode(PIE_Standalone); Settings->SetPlayNumberOfClients(1);
    Settings->SetRunUnderOneProcess(true); Settings->bLaunchSeparateServer = false;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Extraction/Training/L_WeaponRange")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FExtractionInputPIESequence>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
    return true;
}
#endif
