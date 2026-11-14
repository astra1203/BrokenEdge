#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Extraction/ExtractionCombatComponent.h"
#include "BrokenEdge/Extraction/ExtractionInteractionComponent.h"
#include "BrokenEdge/Extraction/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/ExtractionLootActor.h"
#include "BrokenEdge/Extraction/ExtractionPlayerController.h"
#include "BrokenEdge/Extraction/ExtractionState.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "UObject/UnrealType.h"

namespace
{
class FConfigureCombatNetworkMap : public IAutomationLatentCommand
{
public:
    explicit FConfigureCombatNetworkMap(FAutomationTestBase* InTest) : Test(InTest) {}
    bool Update() override
    {
        auto* Mode = LoadClass<AGameModeBase>(nullptr, TEXT("/Script/StingerTools.CombatNetworkTestMode"));
        if (Test->TestNotNull(TEXT("Editor-only network training mode"), Mode))
            GEditor->GetEditorWorldContext().World()->GetWorldSettings()->DefaultGameMode = Mode;
        return true;
    }
private:
    FAutomationTestBase* Test;
};

class FRestoreCombatNetworkSettings : public IAutomationLatentCommand
{
public:
    FRestoreCombatNetworkSettings()
    {
        const auto* S = GetDefault<ULevelEditorPlaySettings>();
        S->GetPlayNetMode(Mode); S->GetPlayNumberOfClients(Clients); S->GetRunUnderOneProcess(OneProcess);
        SeparateServer = S->bLaunchSeparateServer;
    }
    bool Update() override
    {
        auto* S = GetMutableDefault<ULevelEditorPlaySettings>();
        S->SetPlayNetMode(Mode); S->SetPlayNumberOfClients(Clients); S->SetRunUnderOneProcess(OneProcess);
        S->bLaunchSeparateServer = SeparateServer; return true;
    }
private:
    EPlayNetMode Mode = PIE_Standalone;
    int32 Clients = 1;
    bool OneProcess = true, SeparateServer = false;
};

class FCombatStateNetworkSequence : public IAutomationLatentCommand
{
public:
    explicit FCombatStateNetworkSequence(FAutomationTestBase* InTest) : Test(InTest) {}
    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        if (Now - Started > 180 || (Step > 0 && Now - StepAt > 25))
        {
            Test->AddError(FString::Printf(TEXT("Network combat timeout at step %d; reload seen=%d%d%d swap seen=%d%d%d"),
                Step, ReloadSeen[0], ReloadSeen[1], ReloadSeen[2], SwapSeen[0], SwapSeen[1], SwapSeen[2]));
            return true;
        }
        UWorld* Server = nullptr;
        TArray<UWorld*> Clients;
        for (const auto& Context : GEngine->GetWorldContexts())
            if (auto* W = Context.World(); W && Context.WorldType == EWorldType::PIE)
            {
                if (W->GetNetMode() == NM_DedicatedServer) Server = W;
                else if (W->GetNetMode() == NM_Client) Clients.Add(W);
            }
        if (!Server || Clients.Num() != 2) return false;
        auto* PC = Cast<AExtractionPlayerController>(Clients[0]->GetFirstPlayerController());
        auto* Owner = PC ? Cast<ABrokenEdgeCharacter>(PC->GetPawn()) : nullptr;
        if (!Owner || !Owner->GetPlayerState() || (Step < 11 && !Owner->GetEquippedWeapon())) return false;
        const int32 PlayerId = Owner->GetPlayerState()->GetPlayerId();
        auto FindPawn = [PlayerId](UWorld* World) -> ABrokenEdgeCharacter*
        {
            for (TActorIterator<ABrokenEdgeCharacter> It(World); It; ++It)
                if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId() == PlayerId) return *It;
            return nullptr;
        };
        auto* Authority = FindPawn(Server);
        auto* Observer = FindPawn(Clients[1]);
        if (!Authority || !Observer || (Step < 11 && (!Authority->GetEquippedWeapon() || !Observer->GetEquippedWeapon()))) return false;
        ABrokenEdgeCharacter* Pawns[] = {Authority, Owner, Observer};
        auto AllState = [&](ECombatState State)
        {
            for (auto* C : Pawns) if (C->GetCombatState() != State) return false;
            return true;
        };
        if (Step == 11)
        {
            if (Now - StepAt < 5) return false; // Past swap/reload deadlines, without advancing timers artificially.
            for (auto* C : Pawns)
                if (!C->IsDefeated() || C->GetEquippedWeapon() || C->GetCombat()->IsReloadPredicted()
                    || C->GetCombatState() != ECombatState::ECS_Unoccuiped) return false;
            auto* Inventory = Authority->GetExtractionInteraction()->Inventory();
            Test->TestTrue(TEXT("Defeat commits inventory drop exactly once"), Inventory->WasDropped() && Inventory->GetContents().Items.IsEmpty());
            for (const auto& Item : BeforeDefeatItems)
            {
                int32 Copies = 0;
                for (TActorIterator<AExtractionLootActor> It(Server); It; ++It)
                    if (const auto* Dropped = It->GetContents().Find(Item.Id))
                    {
                        ++Copies;
                        Test->TestEqual(TEXT("Dropped item quantity is conserved"), Dropped->Quantity, Item.Quantity);
                        Test->TestEqual(TEXT("Pending reload/swap cannot refill a dropped magazine"), Dropped->Magazine, Item.Magazine);
                    }
                Test->TestEqual(TEXT("Each inventory instance is dropped once"), Copies, 1);
            }
            for (auto* C : Pawns)
            {
                Test->TestFalse(TEXT("Defeat clears held input on all peers"), C->GetCombat()->IsFireInputHeld());
                Test->TestFalse(TEXT("Defeated peers cannot fire"), C->GetCombat()->CanFire());
                Test->TestTrue(TEXT("Interrupted swap unlocks presentation"), C->bFinishedSwapping);
            }
            Test->AddInfo(TEXT("Network combat step 11 passed: defeat during client-requested swap; no delayed action or duplicate inventory drop"));
            return true;
        }
        auto AmmoConverged = [&]
        {
            const auto* Gun = Authority->GetEquippedWeapon();
            for (auto* C : Pawns)
                if (C->GetEquippedWeapon()->ExtractionDefinition != Gun->ExtractionDefinition
                    || C->GetEquippedWeapon()->GetAmmo() != Gun->GetAmmo()) return false;
            return true;
        };
        auto Next = [&]
        {
            Test->AddInfo(FString::Printf(TEXT("Network combat step %d passed; server/owner/observer state=%d/%d/%d ammo=%d/%d/%d"),
                Step, int32(Authority->GetCombatState()), int32(Owner->GetCombatState()), int32(Observer->GetCombatState()),
                Authority->GetEquippedWeapon()->GetAmmo(), Owner->GetEquippedWeapon()->GetAmmo(), Observer->GetEquippedWeapon()->GetAmmo()));
            ++Step; StepAt = Now;
        };
        auto Shoot = [&]
        {
            Owner->GetExtractionCombat()->RequestShot(Owner->GetPawnViewLocation() + Owner->GetBaseAimRotation().Vector() * 10000);
        };
        if (Step == 0)
        {
            if (!Owner->GetExtractionCombat()->CanFire() || !Authority->GetExtractionCombat()->CanFire() || Now - Started < 10) return false;
            for (auto* W : {Server, Clients[0], Clients[1]})
            {
                auto* Driver = W->GetNetDriver();
                if (!Test->TestNotNull(TEXT("Actual network driver"), Driver)) return true;
                auto Settings = Driver->PacketSimulationSettings;
                Settings.PktLag = 150; Settings.PktLagVariance = 0; Settings.PktLoss = 5;
                Driver->SetPacketSimulationSettings(Settings);
                Test->TestEqual(TEXT("150 ms outgoing packet lag"), Driver->PacketSimulationSettings.PktLag, 150);
                Test->TestEqual(TEXT("5 percent packet loss"), Driver->PacketSimulationSettings.PktLoss, 5);
                Test->AddInfo(FString::Printf(TEXT("Network emulation mode=%d outgoing lag=%d loss=%d"),
                    int32(W->GetNetMode()), Driver->PacketSimulationSettings.PktLag, Driver->PacketSimulationSettings.PktLoss));
            }
            InitialAmmo = Authority->GetEquippedWeapon()->GetAmmo();
            InitialReserve = Authority->GetCombat()->GetCarriedAmmo();
            InitialShots = Authority->GetExtractionCombat()->GetAcceptedShots();
            Shoot(); Next();
        }
        else if (Step == 1)
        {
            if (Authority->GetExtractionCombat()->GetAcceptedShots() != InitialShots + 1 || !AmmoConverged()) return false;
            Test->TestEqual(TEXT("One confirmed shot consumes one round on all peers"), Owner->GetEquippedWeapon()->GetAmmo(), InitialAmmo - 1);
            Owner->GetCombat()->Reload();
            Test->TestTrue(TEXT("Owning client predicts reload immediately"), Owner->GetCombat()->IsReloadPredicted());
            Next();
        }
        else if (Step == 2)
        {
            for (int32 I = 0; I < 3; ++I) ReloadSeen[I] |= Pawns[I]->GetCombatState() == ECombatState::ECS_Reloading;
            if (!ReloadSeen[0] || !ReloadSeen[1] || !ReloadSeen[2] || !AllState(ECombatState::ECS_Unoccuiped)
                || !AmmoConverged() || !Authority->GetEquippedWeapon()->IsFull() || Owner->GetCombat()->IsReloadPredicted()) return false;
            Test->TestEqual(TEXT("Reload conserves authority magazine plus reserve"), Authority->GetEquippedWeapon()->GetAmmo()
                + Authority->GetCombat()->GetCarriedAmmo(), InitialAmmo + InitialReserve - 1);
            Shoot(); Next();
        }
        else if (Step == 3)
        {
            if (Authority->GetExtractionCombat()->GetAcceptedShots() != InitialShots + 2 || !AmmoConverged()) return false;
            // Server-only fixture change creates a real rejected owner request before replication arrives.
            Authority->GetExtractionInteraction()->InventoryOpen = true;
            RejectedAmmo = Authority->GetEquippedWeapon()->GetAmmo();
            Owner->GetCombat()->Reload();
            Test->TestTrue(TEXT("Stale owner starts reload prediction before rejection"), Owner->GetCombat()->IsReloadPredicted());
            Next();
        }
        else if (Step == 4)
        {
            if (Now - StepAt < 1 || Owner->GetCombat()->IsReloadPredicted()) return false;
            Test->TestTrue(TEXT("Rejected reload leaves all peers idle"), AllState(ECombatState::ECS_Unoccuiped));
            Test->TestEqual(TEXT("Rejected reload cannot create ammunition"), Authority->GetEquippedWeapon()->GetAmmo(), RejectedAmmo);
            Authority->GetExtractionInteraction()->InventoryOpen = false;
            BeforeSwap = Authority->GetEquippedWeapon()->ExtractionDefinition;
            PC->SelectSecondaryGun(); // Production 2-key handler -> melee selection RPC -> server swap.
            Next();
        }
        else if (Step == 5)
        {
            for (int32 I = 0; I < 3; ++I) SwapSeen[I] |= Pawns[I]->GetCombatState() == ECombatState::ECS_SwappingWeapon;
            if (!SwapSeen[0] || !SwapSeen[1] || !SwapSeen[2] || !AllState(ECombatState::ECS_Unoccuiped)
                || !AmmoConverged() || Authority->GetEquippedWeapon()->ExtractionDefinition == BeforeSwap
                || !Owner->GetExtractionCombat()->CanFire() || !Authority->GetExtractionCombat()->CanFire()) return false;
            for (auto* C : Pawns) Test->TestTrue(TEXT("Swap presentation flag clears on every peer"), C->bFinishedSwapping);
            Test->TestEqual(TEXT("Client secondary-slot request equips SCAR"), Owner->GetEquippedWeapon()->ExtractionDefinition, FName(TEXT("SCARH")));
            Shoot(); Next();
        }
        else if (Step == 6)
        {
            if (Authority->GetExtractionCombat()->GetAcceptedShots() != InitialShots + 3 || !AmmoConverged()) return false;
            InterruptedAmmo = Authority->GetEquippedWeapon()->GetAmmo();
            InterruptedReserve = Authority->GetCombat()->GetCarriedAmmo();
            Owner->GetCombat()->Reload(); Next();
        }
        else if (Step == 7)
        {
            if (!AllState(ECombatState::ECS_Reloading)) return false;
            PC->RequestOpenInventory(nullptr, true); Next();
        }
        else if (Step == 8)
        {
            if (Now - StepAt < 5 || !AllState(ECombatState::ECS_Unoccuiped) || Owner->GetCombat()->IsReloadPredicted()
                || !Authority->GetExtractionInteraction()->InventoryOpen || !Owner->GetExtractionInteraction()->InventoryOpen) return false;
            Test->TestTrue(TEXT("Interrupted reload ammo converges"), AmmoConverged());
            Test->TestEqual(TEXT("Opening inventory cancels delayed magazine refill"), Authority->GetEquippedWeapon()->GetAmmo(), InterruptedAmmo);
            Test->TestEqual(TEXT("Interrupted reload preserves reserve"), Authority->GetCombat()->GetCarriedAmmo(), InterruptedReserve);
            PC->RequestOpenInventory(nullptr, false); Next();
        }
        else if (Step == 9)
        {
            if (Authority->GetExtractionInteraction()->InventoryOpen || Owner->GetExtractionInteraction()->InventoryOpen) return false;
            PC->SelectPrimaryGun(); Next();
        }
        else if (Step == 10)
        {
            if (Authority->GetCombatState() != ECombatState::ECS_SwappingWeapon) return false;
            auto* Delay = FindFProperty<FFloatProperty>(ABrokenEdgeCharacter::StaticClass(), TEXT("RespawnDelay"));
            if (!Test->TestNotNull(TEXT("Delay fixture respawn until interruption checks finish"), Delay)) return true;
            Delay->SetPropertyValue_InContainer(Authority, 60.f);
            auto* Inventory = Authority->GetExtractionInteraction()->Inventory();
            Inventory->CaptureWeapons(); BeforeDefeatItems = Inventory->GetContents().Items;
            Test->TestTrue(TEXT("Death fixture has carried items"), !BeforeDefeatItems.IsEmpty());
            // Advance before defeat: the normal progress logger requires a held weapon.
            Next(); Authority->Defeated(false); Authority->Defeated(false);
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    double Started = FPlatformTime::Seconds(), StepAt = 0;
    int32 Step = 0, InitialAmmo = 0, InitialReserve = 0, RejectedAmmo = 0;
    int32 InterruptedAmmo = 0, InterruptedReserve = 0;
    TArray<FExtractionItem> BeforeDefeatItems;
    uint32 InitialShots = 0;
    bool ReloadSeen[3] = {}, SwapSeen[3] = {};
    FName BeforeSwap;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatStateNetworkPIE, "BrokenEdge.Extraction.CombatNetworkPIE.StateAndAmmo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCombatStateNetworkPIE::RunTest(const FString&)
{
    const auto Restore = MakeShared<FRestoreCombatNetworkSettings>();
    auto* S = GetMutableDefault<ULevelEditorPlaySettings>();
    S->SetPlayNetMode(PIE_Client); S->SetPlayNumberOfClients(2); S->SetRunUnderOneProcess(true);
    S->bLaunchSeparateServer = false;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Extraction/Training/L_WeaponRange")));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FConfigureCombatNetworkMap>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FCombatStateNetworkSequence>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
    return true;
}
#endif
