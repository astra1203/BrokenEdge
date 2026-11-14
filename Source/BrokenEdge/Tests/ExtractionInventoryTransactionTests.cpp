#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ExtractionInventoryTestAccess.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionLootActor.h"
#include "Engine/World.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionInventoryTransactionTest,
    "BrokenEdge.Extraction.Inventory.Transaction.CommitAndRollback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionInventoryTransactionTest::RunTest(const FString&)
{
    const auto* Property = FindFProperty<FProperty>(UExtractionInventoryComponent::StaticClass(), TEXT("Contents"));
    if (!TestNotNull(TEXT("Private inventory retains reflected replication field"), Property)) return false;
    TestTrue(TEXT("Contents remains replicated"), Property->HasAnyPropertyFlags(CPF_Net));
    TestFalse(TEXT("Contents is not exposed to Blueprint graph access"), Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
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
    const auto Initial = FExtractionInventoryTestAccess::Get(Inventory);
    auto Result = Inventory->Mutate([](auto& C, FString&) { ++C.Width; return false; });
    TestTrue(TEXT("Rejected edit"), Result.Code == EExtractionInventoryCommit::Rejected);
    TestTrue(TEXT("Rejected edit leaves all fields intact"), Same(Initial, FExtractionInventoryTestAccess::Get(Inventory)));
    bool Called = false;
    Result = Inventory->Mutate([&](auto&, FString&) { Called = true; return true; }, Initial.Revision + 1);
    TestTrue(TEXT("Stale edit"), Result.Code == EExtractionInventoryCommit::Stale);
    TestFalse(TEXT("Stale edit never invokes callback"), Called);
    Result = Inventory->Mutate([](auto&, FString&) { return true; });
    TestTrue(TEXT("No-op"), Result.Code == EExtractionInventoryCommit::NoChange);
    TestTrue(TEXT("No-op preserves revision"), Same(Initial, FExtractionInventoryTestAccess::Get(Inventory)));
    Result = Inventory->Mutate([](auto& C, FString&) { ++C.Width; return true; });
    TestTrue(TEXT("Successful commit"), Result.Ok());
    TestEqual(TEXT("Revision advances once"), FExtractionInventoryTestAccess::Get(Inventory).Revision, Initial.Revision + 1);
    Result = Inventory->Mutate([](auto& C, FString&) { ++C.Width; ++C.Revision; return true; });
    TestTrue(TEXT("Primitive revision retained"), Result.Ok());
    TestEqual(TEXT("No double increment"), FExtractionInventoryTestAccess::Get(Inventory).Revision, Initial.Revision + 2);

    const auto Before = FExtractionInventoryTestAccess::Get(Inventory), OtherBefore = Loot->GetContents();
    Inventory->FailNextWeaponSyncForTest();
    Result = UExtractionInventoryComponent::MutatePair(*Inventory, *Loot,
        [](auto& A, auto& B, FString&) { ++A.Width; ++B.Width; return true; });
    TestTrue(TEXT("Injected weapon failure"), Result.Code == EExtractionInventoryCommit::WeaponSyncFailed);
    TestTrue(TEXT("Rollback restores inventory and revision"), Same(Before, FExtractionInventoryTestAccess::Get(Inventory)));
    TestTrue(TEXT("Rollback restores loot and revision"), Same(OtherBefore, Loot->GetContents()));
    Result = UExtractionInventoryComponent::MutatePair(*Inventory, *Loot,
        [](auto& A, auto& B, FString&) { ++A.Width; ++B.Width; return true; },
        Before.Revision, OtherBefore.Revision);
    TestTrue(TEXT("Pair commits"), Result.Ok());
    TestEqual(TEXT("Inventory advances once"), FExtractionInventoryTestAccess::Get(Inventory).Revision, Before.Revision + 1);
    TestEqual(TEXT("Loot advances once"), Loot->GetContents().Revision, OtherBefore.Revision + 1);
    Result = UExtractionInventoryComponent::MutatePair(*Inventory, *Loot,
        [](auto&, auto&, FString&) { return true; }, INDEX_NONE, OtherBefore.Revision);
    TestTrue(TEXT("Stale loot rejected"), Result.Code == EExtractionInventoryCommit::Stale);

    auto Ammo = ExtractionItems::Make(TEXT("Ammo"), 20);
    ExtractionItems::SeedOrigins(Ammo);
    TestTrue(TEXT("Seed source stack"), ExtractionItems::AutoPlace(FExtractionInventoryTestAccess::Get(Inventory), Ammo));
    FExtractionItemRequest Request;
    Request.Item = Ammo.Id;
    Request.Quantity = Ammo.Quantity;
    Request.AutoPlace = true;
    const auto TransferBefore = FExtractionInventoryTestAccess::Get(Inventory), TransferOtherBefore = Loot->GetContents();
    auto Transfer = [&](auto& A, auto& B, FString& Error)
    { return ExtractionItems::Transfer(A, B, Request, Error); };
    Inventory->FailNextWeaponSyncForTest();
    Result = UExtractionInventoryComponent::MutatePair(*Inventory, *Loot, Transfer);
    TestTrue(TEXT("Real transfer rolls back on weapon failure"), Result.Code == EExtractionInventoryCommit::WeaponSyncFailed);
    TestTrue(TEXT("Rollback preserves source quantities, GUIDs and origins"), Same(TransferBefore, FExtractionInventoryTestAccess::Get(Inventory)));
    TestTrue(TEXT("Rollback preserves destination quantities, GUIDs and origins"), Same(TransferOtherBefore, Loot->GetContents()));
    Result = UExtractionInventoryComponent::MutatePair(*Inventory, *Loot, Transfer);
    TestTrue(TEXT("Real transfer commits"), Result.Ok());
    TestNull(TEXT("Source relinquishes GUID"), FExtractionInventoryTestAccess::Get(Inventory).Find(Ammo.Id));
    const auto* Received = Loot->GetContents().Find(Ammo.Id);
    if (TestNotNull(TEXT("Destination retains GUID"), Received))
    {
        TestEqual(TEXT("Quantity conserved"), Received->Quantity, Ammo.Quantity);
        TestEqual(TEXT("Origin lot count conserved"), Received->Origins.Num(), Ammo.Origins.Num());
        for (int32 Index = 0; Index < FMath::Min(Received->Origins.Num(), Ammo.Origins.Num()); ++Index)
            TestTrue(TEXT("Origin lot identity and quantity preserved"),
                FExtractionOrigin::StaticStruct()->CompareScriptStruct(&Received->Origins[Index], &Ammo.Origins[Index], 0));
    }
    // Consumables must reject unknown/non-consumable GUIDs and remove exactly one origin unit.
    auto Medkit = ExtractionItems::Make(TEXT("Medkit"));
    TestTrue(TEXT("Place medical item"), ExtractionItems::AutoPlace(FExtractionInventoryTestAccess::Get(Inventory), Medkit));
    const int32 MedicalRevision = FExtractionInventoryTestAccess::Get(Inventory).Revision;
    TestTrue(TEXT("Medical consumption commits"), Inventory->ConsumeOne(Medkit.Id).Ok());
    TestNull(TEXT("Last unit removes medical GUID"), FExtractionInventoryTestAccess::Get(Inventory).Find(Medkit.Id));
    TestEqual(TEXT("Consume advances once"), FExtractionInventoryTestAccess::Get(Inventory).Revision, MedicalRevision + 1);
    const auto AfterMedical = FExtractionInventoryTestAccess::Get(Inventory);
    TestFalse(TEXT("Cannot consume same item twice"), Inventory->ConsumeOne(Medkit.Id).Ok());
    TestTrue(TEXT("Duplicate consumption has no effect"), Same(AfterMedical, FExtractionInventoryTestAccess::Get(Inventory)));

    // A synthetic stack exercises provenance accounting even when current medical assets stack to one.
    Medkit.Quantity = 2;
    Medkit.Origins.Reset();
    ExtractionItems::SeedOrigins(Medkit);
    FExtractionInventoryTestAccess::Get(Inventory).Items.Add(Medkit);
    TestTrue(TEXT("Consume one from stack"), Inventory->ConsumeOne(Medkit.Id).Ok());
    const auto* RemainingMedical = FExtractionInventoryTestAccess::Get(Inventory).Find(Medkit.Id);
    if (TestNotNull(TEXT("Stack GUID retained"), RemainingMedical))
    {
        TestEqual(TEXT("Only one unit consumed"), RemainingMedical->Quantity, 1);
        int32 OriginQuantity = 0;
        for (const auto& Origin : RemainingMedical->Origins) OriginQuantity += Origin.Quantity;
        TestEqual(TEXT("Only one origin unit consumed"), OriginQuantity, 1);
    }

    auto Gun = ExtractionItems::Make(TEXT("AK"));
    Gun.Slot = 0;
    Gun.Magazine = 0;
    Gun.MagazineOrigins.Reset();
    FExtractionInventoryTestAccess::Get(Inventory).WeaponSlots = true;
    FExtractionInventoryTestAccess::Get(Inventory).Items.Add(Gun);
    TestFalse(TEXT("Weapons cannot use ConsumeOne"), Inventory->ConsumeOne(Gun.Id).Ok());
    auto Reserve = ExtractionItems::Make(Gun.MagazineAmmo, 10);
    TestTrue(TEXT("Seed reload reserve"), ExtractionItems::AutoPlace(FExtractionInventoryTestAccess::Get(Inventory), Reserve));
    const auto* GunDef = UExtractionItemDefinition::Get(Gun.Definition);
    auto* Weapon = GunDef ? World->SpawnActor<AWeapon>(GunDef->WeaponClass) : nullptr;
    if (TestNotNull(TEXT("Weapon for ammo integration"), Weapon))
    {
        Weapon->ExtractionItemId = Gun.Id;
        Weapon->SetExtractionAmmo(0);
        const int32 BeforeReload = FExtractionInventoryTestAccess::Get(Inventory).Revision;
        const int32 Loaded = Inventory->Reload(Weapon);
        TestEqual(TEXT("Reload consumes reserve"), Loaded, FMath::Min(10, Weapon->GetMagCapacity()));
        TestTrue(TEXT("Reload adds ammo"), Loaded > 0);
        TestEqual(TEXT("Reload advances once"), FExtractionInventoryTestAccess::Get(Inventory).Revision, BeforeReload + 1);
        TestEqual(TEXT("Actor and inventory magazine agree"), FExtractionInventoryTestAccess::Get(Inventory).Find(Gun.Id)->Magazine, Weapon->GetAmmo());
        Weapon->SetExtractionAmmo(Weapon->GetAmmo() - 1);
        Inventory->RecordShot(Weapon);
        TestEqual(TEXT("Shot advances once"), FExtractionInventoryTestAccess::Get(Inventory).Revision, BeforeReload + 2);
        const auto* FiredGun = FExtractionInventoryTestAccess::Get(Inventory).Find(Gun.Id);
        int32 MagazineOrigins = 0;
        for (const auto& Origin : FiredGun->MagazineOrigins) MagazineOrigins += Origin.Quantity;
        TestEqual(TEXT("Shot consumes one magazine origin"), MagazineOrigins, Weapon->GetAmmo());
    }
    const auto BeforeClear = FExtractionInventoryTestAccess::Get(Inventory);
    // Clear has no guns to restore, isolating the failure boundary from asset spawning during rollback.
    FExtractionInventoryTestAccess::Get(Inventory).Items.RemoveAll([&](const auto& Item) { return Item.Id == Gun.Id; });
    const auto ClearCandidate = FExtractionInventoryTestAccess::Get(Inventory);
    Inventory->FailNextWeaponSyncForTest();
    Inventory->ClearSettled();
    TestTrue(TEXT("Failed clear preserves inventory"), Same(ClearCandidate, FExtractionInventoryTestAccess::Get(Inventory)));
    TestFalse(TEXT("Failed clear does not mark dropped"), Inventory->WasDropped());
    Inventory->ClearSettled();
    TestTrue(TEXT("Successful clear empties inventory"), FExtractionInventoryTestAccess::Get(Inventory).Items.IsEmpty());
    TestTrue(TEXT("Successful clear marks dropped"), Inventory->WasDropped());
    World->DestroyWorld(false);
    return true;
}
#endif
