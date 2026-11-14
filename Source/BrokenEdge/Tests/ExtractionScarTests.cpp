#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionItems.h"
#include "BrokenEdge/Weapon/Weapon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionScarDefinitionTest,
                                 "BrokenEdge.Extraction.Expansion.ScarCaliberIsolation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionScarDefinitionTest::RunTest(const FString&)
{
	const auto* Scar = UExtractionItemDefinition::Get(TEXT("SCARH"));
	const auto* Ak = UExtractionItemDefinition::Get(TEXT("AK"));
	if (!TestNotNull(TEXT("SCAR definition"), Scar) || !TestNotNull(TEXT("AK definition"), Ak)) return false;
	TestTrue(TEXT("SCAR has its own weapon class"), Scar->WeaponClass && Scar->WeaponClass != Ak->WeaponClass);
	TestTrue(TEXT("SCAR accepts 76251"), Scar->SupportsAmmo(TEXT("Ammo76251")));
	TestFalse(TEXT("SCAR rejects AK ammunition"), Scar->SupportsAmmo(TEXT("Ammo")));
	TestFalse(TEXT("AK rejects 76251 ammunition"), Ak->SupportsAmmo(TEXT("Ammo76251")));
	auto Gun = ExtractionItems::Make(TEXT("SCARH"));
	FString Error;
	TestEqual(TEXT("SCAR basic magazine has 20 rounds"), Gun.Magazine, 20);
	TestTrue(TEXT("SCAR provenance validates"), ExtractionItems::ValidateItem(Gun, true, Error));
	Gun.MagazineAmmo = TEXT("Ammo");
	TestFalse(TEXT("Forged caliber rejected on inventory validation"), ExtractionItems::ValidateItem(Gun, true, Error));
	return true;
}
#endif
