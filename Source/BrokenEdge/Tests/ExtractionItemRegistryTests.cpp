#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionItemRegistry.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionItems.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionItemRegistryTest, "BrokenEdge.Extraction.ItemRegistry.IdentityAndLoading",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FExtractionItemRegistryTest::RunTest(const FString&)
{
    const auto* Registry = UExtractionItemRegistry::Get();
    if (!TestNotNull(TEXT("Authored registry loads"), Registry)) return false;
    for (const FString& Error : Registry->ValidateEntries()) AddError(Error);
    TestEqual(TEXT("Runtime whitelist is generated from registry"), UExtractionItemDefinition::Ids().Num(), Registry->Entries.Num());
    for (FName Id : UExtractionItemDefinition::Ids())
        TestEqual(*Id.ToString(), UExtractionItemDefinition::Get(Id), Registry->Resolve(Id));
    TestNull(TEXT("Unknown ID cannot load an arbitrary path"), UExtractionItemDefinition::Get(TEXT("/Game/Other/Asset")));

    auto* Fixture = NewObject<UExtractionItemRegistry>();
    auto* Definition = NewObject<UExtractionItemDefinition>(GetTransientPackage(), TEXT("RenamedAssetUnrelatedToStableId"));
    Definition->ItemId = TEXT("StableSavedId");
    Fixture->Entries.Add(Definition->ItemId, Definition);
    TestEqual(TEXT("Package/object name does not determine identity"), Fixture->Resolve(Definition->ItemId),
        static_cast<const UExtractionItemDefinition*>(Definition));
    TestTrue(TEXT("Renamed asset is valid"), Fixture->ValidateEntries().IsEmpty());
    Fixture->Entries.Add(TEXT("WrongSavedId"), Definition);
    TestNull(TEXT("Wrong mapping cannot reinterpret a saved ID"), Fixture->Resolve(TEXT("WrongSavedId")));
    TestTrue(TEXT("Validation identifies the wrong mapping"), Fixture->ValidateEntries().ContainsByPredicate(
        [](const FString& Error) { return Error.Contains(TEXT("WrongSavedId")) && Error.Contains(TEXT("StableSavedId")); }));
    Fixture->Entries.Add(TEXT("MissingReference"), TSoftObjectPtr<UExtractionItemDefinition>());
    TestNull(TEXT("Null reference fails closed"), Fixture->Resolve(TEXT("MissingReference")));
    TestTrue(TEXT("Missing reference is named"), Fixture->ValidateEntries().ContainsByPredicate(
        [](const FString& Error) { return Error.Contains(TEXT("MissingReference")); }));
    return true;
}
#endif
