#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Extraction/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/ExtractionLootActor.h"
#include "Engine/World.h"
#include "UObject/Class.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionInventoryTransactionTest,
    "BrokenEdge.Extraction.Inventory.Transaction.CommitAndRollback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionInventoryTransactionTest::RunTest(const FString&)
{
    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
                                      ERHIFeatureLevel::Num, &Init);
    if (!TestNotNull(TEXT("World"), World)) return false;
    auto* Character = World->SpawnActor<ABrokenEdgeCharacter>();
    auto* Loot = World->SpawnActor<AExtractionLootActor>();
    if (!TestNotNull(TEXT("Character"), Character) || !TestNotNull(TEXT("Loot"), Loot))
    {
        World->DestroyWorld(false);
        return false;
    }
    auto* Inventory = NewObject<UExtractionInventoryComponent>(Character);
    auto Same = [](const FExtractionContainer& A, const FExtractionContainer& B)
    { return FExtractionContainer::StaticStruct()->CompareScriptStruct(&A, &B, 0); };
    const auto Initial = Inventory->Contents;
    auto Result = Inventory->Mutate([](auto& C, FString&) { ++C.Width; return false; });
    TestTrue(TEXT("Rejected edit"), Result.Code == EExtractionInventoryCommit::Rejected);
    TestTrue(TEXT("Rejected edit leaves all fields intact"), Same(Initial, Inventory->Contents));
    bool Called = false;
    Result = Inventory->Mutate([&](auto&, FString&) { Called = true; return true; }, Initial.Revision + 1);
    TestTrue(TEXT("Stale edit"), Result.Code == EExtractionInventoryCommit::Stale);
    TestFalse(TEXT("Stale edit never invokes callback"), Called);
    Result = Inventory->Mutate([](auto&, FString&) { return true; });
    TestTrue(TEXT("No-op"), Result.Code == EExtractionInventoryCommit::NoChange);
    TestTrue(TEXT("No-op preserves revision"), Same(Initial, Inventory->Contents));
    Result = Inventory->Mutate([](auto& C, FString&) { ++C.Width; return true; });
    TestTrue(TEXT("Successful commit"), Result.Ok());
    TestEqual(TEXT("Revision advances once"), Inventory->Contents.Revision, Initial.Revision + 1);
    Result = Inventory->Mutate([](auto& C, FString&) { ++C.Width; ++C.Revision; return true; });
    TestTrue(TEXT("Primitive revision retained"), Result.Ok());
    TestEqual(TEXT("No double increment"), Inventory->Contents.Revision, Initial.Revision + 2);

    const auto Before = Inventory->Contents, OtherBefore = Loot->Contents;
    Inventory->FailNextWeaponSyncForTest();
    Result = UExtractionInventoryComponent::MutatePair(*Inventory, *Loot,
        [](auto& A, auto& B, FString&) { ++A.Width; ++B.Width; return true; });
    TestTrue(TEXT("Injected weapon failure"), Result.Code == EExtractionInventoryCommit::WeaponSyncFailed);
    TestTrue(TEXT("Rollback restores inventory and revision"), Same(Before, Inventory->Contents));
    TestTrue(TEXT("Rollback restores loot and revision"), Same(OtherBefore, Loot->Contents));
    Result = UExtractionInventoryComponent::MutatePair(*Inventory, *Loot,
        [](auto& A, auto& B, FString&) { ++A.Width; ++B.Width; return true; },
        Before.Revision, OtherBefore.Revision);
    TestTrue(TEXT("Pair commits"), Result.Ok());
    TestEqual(TEXT("Inventory advances once"), Inventory->Contents.Revision, Before.Revision + 1);
    TestEqual(TEXT("Loot advances once"), Loot->Contents.Revision, OtherBefore.Revision + 1);
    Result = UExtractionInventoryComponent::MutatePair(*Inventory, *Loot,
        [](auto&, auto&, FString&) { return true; }, INDEX_NONE, OtherBefore.Revision);
    TestTrue(TEXT("Stale loot rejected"), Result.Code == EExtractionInventoryCommit::Stale);

    auto Ammo = ExtractionItems::Make(TEXT("Ammo"), 20);
    ExtractionItems::SeedOrigins(Ammo);
    TestTrue(TEXT("Seed source stack"), ExtractionItems::AutoPlace(Inventory->Contents, Ammo));
    FExtractionItemRequest Request;
    Request.Item = Ammo.Id;
    Request.Quantity = Ammo.Quantity;
    Request.AutoPlace = true;
    const auto TransferBefore = Inventory->Contents, TransferOtherBefore = Loot->Contents;
    auto Transfer = [&](auto& A, auto& B, FString& Error)
    { return ExtractionItems::Transfer(A, B, Request, Error); };
    Inventory->FailNextWeaponSyncForTest();
    Result = UExtractionInventoryComponent::MutatePair(*Inventory, *Loot, Transfer);
    TestTrue(TEXT("Real transfer rolls back on weapon failure"), Result.Code == EExtractionInventoryCommit::WeaponSyncFailed);
    TestTrue(TEXT("Rollback preserves source quantities, GUIDs and origins"), Same(TransferBefore, Inventory->Contents));
    TestTrue(TEXT("Rollback preserves destination quantities, GUIDs and origins"), Same(TransferOtherBefore, Loot->Contents));
    Result = UExtractionInventoryComponent::MutatePair(*Inventory, *Loot, Transfer);
    TestTrue(TEXT("Real transfer commits"), Result.Ok());
    TestNull(TEXT("Source relinquishes GUID"), Inventory->Contents.Find(Ammo.Id));
    const auto* Received = Loot->Contents.Find(Ammo.Id);
    if (TestNotNull(TEXT("Destination retains GUID"), Received))
    {
        TestEqual(TEXT("Quantity conserved"), Received->Quantity, Ammo.Quantity);
        TestEqual(TEXT("Origin lot count conserved"), Received->Origins.Num(), Ammo.Origins.Num());
        for (int32 Index = 0; Index < FMath::Min(Received->Origins.Num(), Ammo.Origins.Num()); ++Index)
            TestTrue(TEXT("Origin lot identity and quantity preserved"),
                FExtractionOrigin::StaticStruct()->CompareScriptStruct(&Received->Origins[Index], &Ammo.Origins[Index], 0));
    }
    World->DestroyWorld(false);
    return true;
}
#endif
