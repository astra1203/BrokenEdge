#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "JsonObjectConverter.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Extraction/Core/ExtractionPlayerController.h"
#include "BrokenEdge/Extraction/Core/ExtractionState.h"
#include "BrokenEdge/Extraction/Core/ExtractionInteractionComponent.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionLootActor.h"

namespace
{
class FInventoryNetworkMap : public IAutomationLatentCommand
{
public:
    explicit FInventoryNetworkMap(FAutomationTestBase* InTest) : Test(InTest) {}
    bool Update() override
    {
        auto* Mode = LoadClass<AGameModeBase>(nullptr, TEXT("/Script/StingerTools.CombatNetworkTestMode"));
        if (Test->TestNotNull(TEXT("Network training fixture mode"), Mode))
            GEditor->GetEditorWorldContext().World()->GetWorldSettings()->DefaultGameMode = Mode;
        return true;
    }
private:
    FAutomationTestBase* Test;
};

class FRestoreInventoryNetworkSettings : public IAutomationLatentCommand
{
public:
    FRestoreInventoryNetworkSettings()
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

class FInventoryNetworkSequence : public IAutomationLatentCommand
{
public:
    explicit FInventoryNetworkSequence(FAutomationTestBase* InTest) : Test(InTest) {}
    bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        if (Now - Started > 180 || (Step > 0 && Now - StepAt > 25))
        { Test->AddError(FString::Printf(TEXT("Network inventory timeout at step %d"), Step)); return true; }
        UWorld* Server = nullptr;
        TArray<UWorld*> Clients;
        for (const auto& Context : GEngine->GetWorldContexts())
            if (auto* W = Context.World(); W && Context.WorldType == EWorldType::PIE)
            {
                if (W->GetNetMode() == NM_DedicatedServer) Server = W;
                else if (W->GetNetMode() == NM_Client) Clients.Add(W);
            }
        if (!Server || Clients.Num() != 2) return false;
        AExtractionPlayerController* PC[2] = {};
        ABrokenEdgeCharacter* Owner[2] = {}, *Authority[2] = {};
        UExtractionInteractionComponent* View[2] = {};
        UExtractionInventoryComponent* Inventory[2] = {}, *OwnerInventory[2] = {};
        int32 PlayerId[2] = {};
        auto FindPawn = [](UWorld* W, int32 Id) -> ABrokenEdgeCharacter*
        {
            for (TActorIterator<ABrokenEdgeCharacter> It(W); It; ++It)
                if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId() == Id) return *It;
            return nullptr;
        };
        for (int32 I = 0; I < 2; ++I)
        {
            PC[I] = Cast<AExtractionPlayerController>(Clients[I]->GetFirstPlayerController());
            Owner[I] = PC[I] ? Cast<ABrokenEdgeCharacter>(PC[I]->GetPawn()) : nullptr;
            if (!Owner[I] || !Owner[I]->GetPlayerState()) return false;
            PlayerId[I] = Owner[I]->GetPlayerState()->GetPlayerId();
            Authority[I] = FindPawn(Server, PlayerId[I]);
            if (!Authority[I]) return false;
            View[I] = Owner[I]->GetExtractionInteraction();
            Inventory[I] = Authority[I]->GetExtractionInteraction()->Inventory();
            OwnerInventory[I] = View[I]->Inventory();
            if (!Inventory[I] || !OwnerInventory[I]) return false;
        }
        auto Next = [&]
        {
            Test->AddInfo(FString::Printf(TEXT("Network inventory step %d passed; revisions=%d/%d loot=%d"),
                Step, Inventory[0]->GetContents().Revision, Inventory[1]->GetContents().Revision,
                Box.IsValid() ? Box->GetContents().Revision : -1));
            ++Step; StepAt = Now;
        };
        auto Json = [](const FExtractionContainer& C, bool ReplicatedOnly = false)
        {
            FString S;
            FJsonObjectConverter::UStructToJsonObjectString(C, S, 0, ReplicatedOnly ? CPF_RepSkip : 0);
            return S;
        };
        auto Converged = [&]
        {
            for (int32 I = 0; I < 2; ++I)
                if (Json(Inventory[I]->GetContents(), true) != Json(OwnerInventory[I]->GetContents(), true)) return false;
            return true;
        };
        auto Conserved = [&]
        {
            for (const auto& Id : Items)
            {
                int32 Count = 0;
                for (const auto* C : {&Box->GetContents(), &Inventory[0]->GetContents(), &Inventory[1]->GetContents()})
                    for (const auto& Item : C->Items) if (Item.Id == Id)
                    {
                        ++Count; Test->TestEqual(TEXT("Fixture quantity conserved"), Item.Quantity, 1);
                        if (Test->TestEqual(TEXT("One provenance entry remains"), Item.Origins.Num(), 1))
                        {
                            Test->TestEqual(TEXT("Transfer preserves the source lot"), Item.Origins[0].Lot, Lots.FindChecked(Id));
                            Test->TestEqual(TEXT("Transfer preserves source quantity"), Item.Origins[0].Quantity, 1);
                        }
                    }
                Test->TestEqual(TEXT("Each loot identity has exactly one authority container"), Count, 1);
            }
        };
        auto MakeRequest = [&](int32 I, FGuid Item)
        {
            FExtractionItemRequest R;
            R.Sequence = ++Sequences[I]; R.Session = View[I]->Session; R.Item = Item; R.FromLoot = true;
            R.InventoryRevision = OwnerInventory[I]->GetContents().Revision; R.LootRevision = View[I]->LootView.Revision;
            return R;
        };
        if (Step == 0)
        {
            if (Now - Started < 10 || !Converged() || !Authority[0]->GetEquippedWeapon() || !Authority[1]->GetEquippedWeapon()) return false;
            for (auto* W : {Server, Clients[0], Clients[1]})
            {
                auto* Driver = W->GetNetDriver();
                if (!Test->TestNotNull(TEXT("Inventory uses a real network driver"), Driver)) return true;
                auto Settings = Driver->PacketSimulationSettings;
                Settings.PktLag = 150; Settings.PktLagVariance = 0; Settings.PktLoss = 5;
                Driver->SetPacketSimulationSettings(Settings);
                Test->TestEqual(TEXT("Inventory packet lag"), Driver->PacketSimulationSettings.PktLag, 150);
                Test->TestEqual(TEXT("Inventory packet loss"), Driver->PacketSimulationSettings.PktLoss, 5);
            }
            Authority[1]->SetActorLocation(Authority[0]->GetActorLocation() + FVector(0, 120, 0), false, nullptr, ETeleportType::TeleportPhysics);
            Authority[1]->GetCharacterMovement()->StopMovementImmediately(); Authority[1]->ForceNetUpdate();
            Box = AExtractionLootActor::SpawnContainer(Server, Authority[0]->GetActorLocation() + FVector(110, 60, -58), false);
            if (!Test->TestNotNull(TEXT("Authority loot fixture"), Box.Get())) return true;
            FExtractionContainer Contents; Contents.Id = Box->GetContents().Id;
            for (int32 I = 0; I < 3; ++I)
            {
                auto Item = ExtractionItems::Make(TEXT("Tools")); Items.Add(Item.Id);
                if (!Test->TestEqual(TEXT("Fixture records one source lot"), Item.Origins.Num(), 1)) return true;
                Lots.Add(Item.Id, Item.Origins[0].Lot);
                if (!Test->TestTrue(TEXT("Fixture tools fit"), ExtractionItems::AutoPlace(Contents, Item))) return true;
            }
            Box->SetContents(Contents);
            for (int32 I = 0; I < 2; ++I) InitialRevision[I] = Inventory[I]->GetContents().Revision;
            InitialLootRevision = Box->GetContents().Revision;
            Next();
        }
        else if (Step == 1)
        {
            AExtractionLootActor* Copies[2] = {};
            for (int32 I = 0; I < 2; ++I)
                for (TActorIterator<AExtractionLootActor> It(Clients[I]); It; ++It)
                    if (It->ContainerId == Box->ContainerId) Copies[I] = *It;
            if (!Copies[0] || !Copies[1]) return false;
            for (int32 I = 0; I < 2; ++I)
            {
                Test->TestTrue(TEXT("World actor does not replicate private contents"), Copies[I]->GetContents().Items.IsEmpty());
                if (!Test->TestTrue(TEXT("Both players can reach the box"), Authority[I]->GetExtractionInteraction()->CanReach(Box.Get()))) return true;
                PC[I]->RequestOpenInventory(Copies[I], true);
            }
            Next();
        }
        else if (Step == 2)
        {
            if (!View[0]->InventoryOpen || !View[1]->InventoryOpen || View[0]->LootView.Items.Num() != 3 || View[1]->LootView.Items.Num() != 3) return false;
            bool Hidden = false;
            for (const auto& Item : View[0]->LootView.Items)
                if (Item.Definition.IsNone())
                { Hidden = true; Test->TestFalse(TEXT("Unsearched identity is hidden"), Item.Id.IsValid()); Test->TestEqual(TEXT("Unsearched quantity is hidden"), Item.Quantity, 0); }
            if (!Test->TestTrue(TEXT("Initial search view masks undiscovered items"), Hidden)) return true;
            PC[0]->ServerItemOperation(MakeRequest(0, Items.Last()));
            Next();
        }
        else if (Step == 3)
        {
            if (PC[0]->ItemResultSequence < Sequences[0]) return false;
            Test->TestFalse(TEXT("Forged unrevealed item request rejected"), PC[0]->ItemMessage.IsEmpty());
            Test->TestEqual(TEXT("Rejected request preserves inventory revision"), Inventory[0]->GetContents().Revision, InitialRevision[0]);
            Test->TestEqual(TEXT("Rejected request preserves loot revision"), Box->GetContents().Revision, InitialLootRevision);
            Conserved(); Next();
        }
        else if (Step == 4)
        {
            for (int32 I = 0; I < 2; ++I) for (const auto& Id : Items) if (!View[I]->LootView.Find(Id)) return false;
            for (int32 I = 0; I < 2; ++I)
            {
                auto* Other = FindPawn(Clients[1 - I], PlayerId[I]);
                if (!Test->TestNotNull(TEXT("Observer sees the other pawn"), Other)) return true;
                Test->TestTrue(TEXT("Observer cannot read the other inventory"), Other->GetExtractionInteraction()->Inventory()->GetContents().Items.IsEmpty());
                Test->TestTrue(TEXT("Observer cannot read the other search view"), Other->GetExtractionInteraction()->LootView.Items.IsEmpty());
                for (const auto& Item : OwnerInventory[I]->GetContents().Items)
                {
                    Test->TestTrue(TEXT("Even owners do not receive server provenance"), Item.Origins.IsEmpty() && Item.MagazineOrigins.IsEmpty());
                    for (const auto& Part : Item.Attachments) Test->TestTrue(TEXT("Attachment provenance stays on the server"), Part.Origins.IsEmpty());
                }
                Race[I] = MakeRequest(I, Items[0]);
            }
            PC[0]->ServerItemOperation(Race[0]); PC[1]->ServerItemOperation(Race[1]);
            Next();
        }
        else if (Step == 5)
        {
            if (PC[0]->ItemResultSequence < Sequences[0] || PC[1]->ItemResultSequence < Sequences[1] || !Converged()) return false;
            const bool A = Inventory[0]->GetContents().Find(Items[0]) != nullptr, B = Inventory[1]->GetContents().Find(Items[0]) != nullptr;
            if (!Test->TestTrue(TEXT("Concurrent take has exactly one winner"), A != B)) return true;
            Winner = A ? 0 : 1;
            for (int32 I = 0; I < 2; ++I) Test->TestEqual(TEXT("Only the winner advances inventory revision once"), Inventory[I]->GetContents().Revision, InitialRevision[I] + (I == Winner ? 1 : 0));
            Test->TestEqual(TEXT("Concurrent take advances loot once"), Box->GetContents().Revision, InitialLootRevision + 1);
            Test->TestTrue(TEXT("Winning request reports success"), PC[Winner]->ItemMessage.IsEmpty());
            Test->TestFalse(TEXT("Losing request reports rejection"), PC[1 - Winner]->ItemMessage.IsEmpty());
            Conserved(); BeforeInventory = Json(Inventory[Winner]->GetContents()); BeforeLoot = Json(Box->GetContents());
            PC[Winner]->ServerItemOperation(Race[Winner]);
            Next();
        }
        else if (Step == 6)
        {
            if (PC[Winner]->ItemMessage.IsEmpty()) return false;
            Test->TestEqual(TEXT("Duplicate keeps inventory unchanged"), Json(Inventory[Winner]->GetContents()), BeforeInventory);
            Test->TestEqual(TEXT("Duplicate keeps loot unchanged"), Json(Box->GetContents()), BeforeLoot);
            auto R = MakeRequest(Winner, Items[1]); R.InventoryRevision = InitialRevision[Winner];
            PC[Winner]->ServerItemOperation(R); Next();
        }
        else if (Step == 7)
        {
            if (PC[Winner]->ItemResultSequence < Sequences[Winner]) return false;
            Test->TestFalse(TEXT("Stale inventory revision rejected"), PC[Winner]->ItemMessage.IsEmpty());
            Test->TestEqual(TEXT("Stale request keeps inventory unchanged"), Json(Inventory[Winner]->GetContents()), BeforeInventory);
            Test->TestEqual(TEXT("Stale request keeps loot unchanged"), Json(Box->GetContents()), BeforeLoot);
            if (View[Winner]->LootView.Revision != Box->GetContents().Revision) return false;
            Inventory[Winner]->FailNextWeaponSyncForTest();
            PC[Winner]->ServerItemOperation(MakeRequest(Winner, Items[1])); Next();
        }
        else if (Step == 8)
        {
            if (PC[Winner]->ItemResultSequence < Sequences[Winner]) return false;
            Test->TestFalse(TEXT("Weapon sync failure reaches the requesting client"), PC[Winner]->ItemMessage.IsEmpty());
            Test->TestEqual(TEXT("Failed pair commit restores inventory exactly"), Json(Inventory[Winner]->GetContents()), BeforeInventory);
            Test->TestEqual(TEXT("Failed pair commit restores loot exactly"), Json(Box->GetContents()), BeforeLoot);
            Conserved(); PC[Winner]->ServerItemOperation(MakeRequest(Winner, Items[1])); Next();
        }
        else if (Step == 9)
        {
            if (PC[Winner]->ItemResultSequence < Sequences[Winner] || !Converged()) return false;
            Test->TestTrue(TEXT("New request succeeds after rollback"), PC[Winner]->ItemMessage.IsEmpty());
            Test->TestNotNull(TEXT("Retry transfers the same item identity"), Inventory[Winner]->GetContents().Find(Items[1]));
            Test->TestEqual(TEXT("Successful retry advances inventory once"), Inventory[Winner]->GetContents().Revision, InitialRevision[Winner] + 2);
            Test->TestEqual(TEXT("Successful retry advances loot once"), Box->GetContents().Revision, InitialLootRevision + 2);
            Conserved(); BeforeInventory = Json(Inventory[Winner]->GetContents()); BeforeLoot = Json(Box->GetContents());
            ClosedSession = View[Winner]->Session;
            PC[Winner]->RequestOpenInventory(nullptr, false); Next();
        }
        else if (Step == 10)
        {
            if (View[Winner]->InventoryOpen || Authority[Winner]->GetExtractionInteraction()->InventoryOpen) return false;
            auto R = MakeRequest(Winner, Items[2]); R.Session = ClosedSession; R.LootRevision = Box->GetContents().Revision;
            PC[Winner]->ServerItemOperation(R); Next();
        }
        else if (Step == 11)
        {
            if (PC[Winner]->ItemResultSequence < Sequences[Winner]) return false;
            Test->TestFalse(TEXT("Closed search session cannot take another item"), PC[Winner]->ItemMessage.IsEmpty());
            Test->TestEqual(TEXT("Closed session preserves inventory"), Json(Inventory[Winner]->GetContents()), BeforeInventory);
            Test->TestEqual(TEXT("Closed session preserves loot"), Json(Box->GetContents()), BeforeLoot);
            Conserved(); Next(); return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    double Started = FPlatformTime::Seconds(), StepAt = 0;
    int32 Step = 0, Winner = INDEX_NONE, InitialRevision[2] = {}, InitialLootRevision = 0;
    uint32 Sequences[2] = {}, ClosedSession = 0;
    TWeakObjectPtr<AExtractionLootActor> Box;
    TArray<FGuid> Items;
    TMap<FGuid, FGuid> Lots;
    FExtractionItemRequest Race[2];
    FString BeforeInventory, BeforeLoot;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionInventoryNetworkPIE, "BrokenEdge.Extraction.NetworkInventoryPIE.Transactions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FExtractionInventoryNetworkPIE::RunTest(const FString&)
{
    const auto Restore = MakeShared<FRestoreInventoryNetworkSettings>();
    auto* S = GetMutableDefault<ULevelEditorPlaySettings>();
    S->SetPlayNetMode(PIE_Client); S->SetPlayNumberOfClients(2); S->SetRunUnderOneProcess(true);
    S->bLaunchSeparateServer = false;
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Extraction/Training/L_WeaponRange")));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FInventoryNetworkMap>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FInventoryNetworkSequence>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
    return true;
}
#endif
