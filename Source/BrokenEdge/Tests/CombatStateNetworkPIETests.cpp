#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "BrokenEdge/Component/BuffComponent.h"
#include "BrokenEdge/Extraction/Combat/ExtractionAbilities.h"
#include "BrokenEdge/Extraction/Health/ExtractionHealth.h"
#include "GameFramework/WorldSettings.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Extraction/Combat/ExtractionCombatComponent.h"
#include "BrokenEdge/Extraction/Core/ExtractionInteractionComponent.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionLootActor.h"
#include "BrokenEdge/Extraction/Core/ExtractionPlayerController.h"
#include "BrokenEdge/Extraction/Core/ExtractionState.h"
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
class FVitalsNetworkSequence : public IAutomationLatentCommand
{
public:
    explicit FVitalsNetworkSequence(FAutomationTestBase* InTest) : Test(InTest) {}
    ~FVitalsNetworkSequence() override
    {
        if (Watched.IsValid()) Watched->OnVitalsChanged.Remove(Handle);
    }
    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        if (Now - Started > 180 || (Step > 0 && Now - StepAt > 25))
        { Test->AddError(FString::Printf(TEXT("Network vitals timeout at step %d"), Step)); return true; }
        UWorld* Server = nullptr;
        TArray<UWorld*> Clients;
        for (const auto& Context : GEngine->GetWorldContexts())
            if (auto* W = Context.World(); W && Context.WorldType == EWorldType::PIE)
            {
                if (W->GetNetMode() == NM_DedicatedServer) Server = W;
                else if (W->GetNetMode() == NM_Client) Clients.Add(W);
            }
        if (!Server || Clients.Num() != 2) return false;
        auto* OwnerPC = Cast<AExtractionPlayerController>(Clients[0]->GetFirstPlayerController());
        auto* MatePC = Cast<AExtractionPlayerController>(Clients[1]->GetFirstPlayerController());
        auto* Owner = OwnerPC ? Cast<ABrokenEdgeCharacter>(OwnerPC->GetPawn()) : nullptr;
        auto* MateOwner = MatePC ? Cast<ABrokenEdgeCharacter>(MatePC->GetPawn()) : nullptr;
        if (!Owner || !MateOwner || !Owner->GetPlayerState() || !MateOwner->GetPlayerState()) return false;
        auto FindPawn = [](UWorld* World, int32 PlayerId) -> ABrokenEdgeCharacter*
        {
            for (TActorIterator<ABrokenEdgeCharacter> It(World); It; ++It)
                if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId() == PlayerId) return *It;
            return nullptr;
        };
        const int32 Id = Owner->GetPlayerState()->GetPlayerId();
        auto* Authority = FindPawn(Server, Id);
        auto* Observer = FindPawn(Clients[1], Id);
        auto* Mate = FindPawn(Server, MateOwner->GetPlayerState()->GetPlayerId());
        if (!Authority || !Observer || !Mate) return false;
        ABrokenEdgeCharacter* Pawns[] = {Authority, Owner, Observer};
        auto* Inventory = Authority->GetExtractionInteraction()->Inventory();
        auto* OwnerInventory = Owner->GetExtractionInteraction()->Inventory();
        auto* PS = Authority->GetPlayerState<AExtractionPlayerState>();
        auto* MatePS = Mate->GetPlayerState<AExtractionPlayerState>();
        if (!Inventory || !OwnerInventory || !PS || !MatePS) return false;
        auto AllVitals = [&](float Health, float Shield)
        {
            for (auto* C : Pawns)
                if (!FMath::IsNearlyEqual(C->GetHealth(), Health, .01f) || !FMath::IsNearlyEqual(C->GetShield(), Shield, .01f)) return false;
            return true;
        };
        auto AllLife = [&](EExtractionLife Life, float DownHealth)
        {
            for (auto* C : Pawns)
            {
                const auto* State = C->GetPlayerState<AExtractionPlayerState>();
                if (!State || State->Life != Life || !FMath::IsNearlyEqual(State->DownHealth, DownHealth, .01f)) return false;
            }
            return true;
        };
        auto Next = [&]
        {
            Test->AddInfo(FString::Printf(TEXT("Network vitals step %d passed; HP=%.2f/%.2f/%.2f Shield=%.2f/%.2f/%.2f Down=%.2f"),
                Step, Authority->GetHealth(), Owner->GetHealth(), Observer->GetHealth(),
                Authority->GetShield(), Owner->GetShield(), Observer->GetShield(), PS->DownHealth));
            ++Step; StepAt = Now;
        };
        auto* Health = Authority->GetExtractionHealth();
        if (Step == 0)
        {
            if (Now - Started < 10 || !Authority->GetEquippedWeapon() || !Owner->GetEquippedWeapon()
                || !Mate->GetExtractionAbilities()->CanStart()) return false;
            for (auto* W : {Server, Clients[0], Clients[1]})
            {
                auto* Driver = W->GetNetDriver();
                if (!Test->TestNotNull(TEXT("Vitals uses a real network driver"), Driver)) return true;
                auto Settings = Driver->PacketSimulationSettings;
                Settings.PktLag = 150; Settings.PktLagVariance = 0; Settings.PktLoss = 5;
                Driver->SetPacketSimulationSettings(Settings);
                Test->TestEqual(TEXT("Vitals packet lag"), Driver->PacketSimulationSettings.PktLag, 150);
                Test->TestEqual(TEXT("Vitals packet loss"), Driver->PacketSimulationSettings.PktLoss, 5);
            }
            PS->ExtractionSquad = 123; MatePS->ExtractionSquad = 123;
            PS->ForceNetUpdate(); MatePS->ForceNetUpdate();
            Mate->SetActorLocation(Authority->GetActorLocation() + FVector(0, 120, 0), false, nullptr, ETeleportType::TeleportPhysics);
            Mate->GetCharacterMovement()->StopMovementImmediately(); Mate->ForceNetUpdate();
            Authority->ApplyVitalsChange({100 - Authority->GetHealth(), 25 - Authority->GetShield(), EExtractionVitalsReason::Restore});
            auto Armor = ExtractionItems::Make(TEXT("Vest2")); Armor.Slot = 3; Armor.Durability = 3; ArmorId = Armor.Id;
            if (!Test->TestTrue(TEXT("Network armor fixture commits"), Inventory->Mutate([&](FExtractionContainer& Candidate, FString&)
                { Candidate.Items.RemoveAll([](const FExtractionItem& Item) { return Item.Slot == 3; }); Candidate.Items.Add(Armor); return true; }, INDEX_NONE, false).Ok())) return true;
            ArmorRevision = Inventory->GetContents().Revision;
            Watched = Authority;
            Handle = Authority->OnVitalsChanged.AddLambda([this](const FExtractionVitalsChange&) { ++Events; });
            Next();
        }
        else if (Step == 1)
        {
            const auto* Armor = OwnerInventory->GetContents().Find(ArmorId);
            if (!AllVitals(100, 25) || !Armor || Armor->Durability != 3) return false;
            Test->TestFalse(TEXT("Owner cannot directly write authoritative vitals"), Owner->ApplyVitalsChange({-50, -10, EExtractionVitalsReason::Restore}).Applied);
            Test->TestFalse(TEXT("Observer cannot directly write authoritative vitals"), Observer->ApplyVitalsChange({-50, -10, EExtractionVitalsReason::Restore}).Applied);
            Test->TestTrue(TEXT("Rejected client writes preserve all peers"), AllVitals(100, 25));
            Events = 0; Authority->TestDamageCalculations = 0;
            Health->Bullet(20, TEXT("spine_01"), TEXT("Ammo"), Mate->GetController(), nullptr);
            Test->TestEqual(TEXT("Friendly hit calculates once"), Authority->TestDamageCalculations, 1);
            Test->TestEqual(TEXT("Friendly hit emits no mutation"), Events, 0);
            Test->TestEqual(TEXT("Friendly hit preserves armor revision"), Inventory->GetContents().Revision, ArmorRevision);
            Authority->TestDamageCalculations = 0;
            Health->Bullet(20, TEXT("spine_01"), TEXT("Ammo"), nullptr, nullptr);
            Test->TestEqual(TEXT("Armor hit calculates once"), Authority->TestDamageCalculations, 1);
            Test->TestEqual(TEXT("Armor hit emits one vitals mutation"), Events, 1);
            Next();
        }
        else if (Step == 2)
        {
            const auto* Armor = OwnerInventory->GetContents().Find(ArmorId);
            if (!AllVitals(83, 25) || !Armor || Armor->Durability != 0 || OwnerInventory->GetContents().Revision != ArmorRevision + 1) return false;
            auto* RemoteInventory = Observer->GetExtractionInteraction()->Inventory();
            Test->TestTrue(TEXT("Observer does not receive private inventory items"), RemoteInventory && RemoteInventory->GetContents().Items.IsEmpty());
            Events = 0;
            Test->TestTrue(TEXT("Armor-breaking hit causes trauma"), Health->Bleeding);
            Test->TestTrue(TEXT("Bandage treats trauma before timed healing"), Health->Treat(UExtractionItemDefinition::Get(TEXT("Bandage"))));
            Test->TestEqual(TEXT("Bandage does not fabricate an HP change"), Events, 0);
            Test->TestTrue(TEXT("Authority medical treatment accepted"), Health->Treat(UExtractionItemDefinition::Get(TEXT("Medkit"))));
            Test->TestEqual(TEXT("Treatment notifies once"), Events, 1);
            Next();
        }
        else if (Step == 3)
        {
            if (!AllVitals(100, 25)) return false;
            for (auto* C : Pawns) if (C->GetExtractionHealth()->Bleeding) return false;
            Events = 0;
            Authority->ApplyVitalsChange({-50, 15, EExtractionVitalsReason::Restore});
            Test->TestEqual(TEXT("Combined HP and shield authority write notifies once"), Events, 1);
            Next();
        }
        else if (Step == 4)
        {
            if (!AllVitals(50, 40)) return false;
            auto* Buff = Authority->FindComponentByClass<UBuffComponent>();
            if (!Test->TestNotNull(TEXT("Network character buff component"), Buff)) return true;
            Buff->BuffHeal(10, 1); Buff->ReplenishShield(10, 1);
            Next();
        }
        else if (Step == 5)
        {
            if (Now - StepAt < 1.5 || !AllVitals(60, 50)) return false;
            Events = 0; Authority->TestDamageCalculations = 0;
            Health->Explosion(200, nullptr, nullptr);
            Test->TestEqual(TEXT("Downing calculates damage once"), Authority->TestDamageCalculations, 1);
            Test->TestEqual(TEXT("Downing notifies once"), Events, 1);
            Next();
        }
        else if (Step == 6)
        {
            if (!AllVitals(0, 50) || !AllLife(EExtractionLife::Downed, GetDefault<UExtractionRules>()->DownHealth)) return false;
            Events = 0; Authority->TestDamageCalculations = 0;
            Test->TestFalse(TEXT("Downed treatment rejected"), Health->Treat(UExtractionItemDefinition::Get(TEXT("Medkit"))));
            Test->TestEqual(TEXT("Rejected treatment does not notify"), Events, 0);
            Health->Explosion(20, nullptr, nullptr);
            Test->TestEqual(TEXT("Downed damage calculates once"), Authority->TestDamageCalculations, 1);
            Test->TestEqual(TEXT("Downed damage notifies once"), Events, 1);
            Next();
        }
        else if (Step == 7)
        {
            if (!AllLife(EExtractionLife::Downed, GetDefault<UExtractionRules>()->DownHealth - 20)) return false;
            Test->AddInfo(FString::Printf(TEXT("Rescue preflight: server=%s client=%s target=%s canStart=%d/%d friendly=%d reach=%d distance=%.2f velocity=%.2f falling=%d"),
                *Mate->GetActorLocation().ToCompactString(), *MateOwner->GetActorLocation().ToCompactString(), *Authority->GetActorLocation().ToCompactString(),
                Mate->GetExtractionAbilities()->CanStart(), MateOwner->GetExtractionAbilities()->CanStart(), UExtractionAbilities::Friendly(Mate, Authority),
                UExtractionAbilities::Reach(Mate, Authority, GetDefault<UExtractionRules>()->ReviveRange),
                FVector::Dist(Mate->GetActorLocation(), Authority->GetActorLocation()), Mate->GetVelocity().Size(), Mate->GetCharacterMovement()->IsFalling()));
            Events = 0;
            MateOwner->GetExtractionAbilities()->Request(EExtractionAction::Revive, Observer);
            Next();
        }
        else if (Step == 8)
        {
            const int32 Actions = static_cast<int32>(Mate->GetExtractionAbilities()->Action) * 10 + static_cast<int32>(MateOwner->GetExtractionAbilities()->Action);
            if (Actions != LastRescueActions)
            {
                LastRescueActions = Actions;
                Test->AddInfo(FString::Printf(TEXT("Rescue action observation=%d target=%s rescuer=%s"), Actions,
                    *GetNameSafe(Mate->GetExtractionAbilities()->Target), *GetNameSafe(Authority->GetExtractionAbilities()->Rescuer)));
            }
            if (Mate->GetExtractionAbilities()->Action != EExtractionAction::Revive
                || MateOwner->GetExtractionAbilities()->Action != EExtractionAction::Revive) return false;
            Test->TestEqual(TEXT("Client rescue RPC targets the authority pawn"), Mate->GetExtractionAbilities()->Target.Get(), Authority);
            MateOwner->GetExtractionAbilities()->ReleaseRescue();
            Next();
        }
        else if (Step == 9)
        {
            if (Mate->GetExtractionAbilities()->Action != EExtractionAction::None || MateOwner->GetExtractionAbilities()->Action != EExtractionAction::None) return false;
            Test->TestNull(TEXT("Cancellation releases the rescue lock"), Authority->GetExtractionAbilities()->Rescuer.Get());
            Test->TestEqual(TEXT("Cancelled rescue restores no health"), Events, 0);
            // Replay the first request after the newer release. The production RPC must reject it.
            MateOwner->GetExtractionAbilities()->ServerBegin(1, EExtractionAction::Revive, Observer, false);
            Next();
        }
        else if (Step == 10)
        {
            if (Now - StepAt < 1) return false;
            Test->TestEqual(TEXT("Old rescue request remains cancelled"), Mate->GetExtractionAbilities()->Action, EExtractionAction::None);
            Test->TestEqual(TEXT("Old rescue request cannot heal"), Events, 0);
            MateOwner->GetExtractionAbilities()->Request(EExtractionAction::Revive, Observer);
            Next();
        }
        else if (Step == 11)
        {
            if (Mate->GetExtractionAbilities()->Action != EExtractionAction::Revive || MateOwner->GetExtractionAbilities()->Action != EExtractionAction::Revive) return false;
            Next();
        }
        else if (Step == 12)
        {
            if (!AllVitals(GetDefault<UExtractionRules>()->ReviveHealth, 0) || !AllLife(EExtractionLife::Alive, 0)
                || Mate->GetExtractionAbilities()->Action != EExtractionAction::None || MateOwner->GetExtractionAbilities()->Action != EExtractionAction::None) return false;
            Test->TestEqual(TEXT("Normal timed rescue restores HP and shield in one authority notification"), Events, 1);
            Test->TestEqual(TEXT("Healing and rescue do not mutate inventory"), OwnerInventory->GetContents().Revision, ArmorRevision + 1);
            for (auto* C : Pawns) Test->TestFalse(TEXT("Revival clears trauma on every peer"), C->GetExtractionHealth()->Bleeding);
            Next();
            return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    double Started = FPlatformTime::Seconds(), StepAt = 0;
    int32 Step = 0, Events = 0, ArmorRevision = 0, LastRescueActions = INDEX_NONE;
    FGuid ArmorId;
    TWeakObjectPtr<ABrokenEdgeCharacter> Watched;
    FDelegateHandle Handle;
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVitalsNetworkPIE, "BrokenEdge.Extraction.VitalsNetworkPIE.ReplicationAndRescue",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVitalsNetworkPIE::RunTest(const FString&)
{
    const auto Restore = MakeShared<FRestoreCombatNetworkSettings>();
    auto* S = GetMutableDefault<ULevelEditorPlaySettings>();
    S->SetPlayNetMode(PIE_Client); S->SetPlayNumberOfClients(2); S->SetRunUnderOneProcess(true);
    S->bLaunchSeparateServer = false;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Extraction/Training/L_WeaponRange")));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FConfigureCombatNetworkMap>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FVitalsNetworkSequence>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
    return true;
}
#endif
