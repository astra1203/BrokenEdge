#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Extraction/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/ExtractionPersistence.h"
#include "Engine/GameInstance.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionStartingSupplyTest,
                                 "BrokenEdge.Extraction.Expansion.StartingSupply",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionStartingSupplyTest::RunTest(const FString&)
{
	FExtractionContainer Bag;
	Bag.WeaponSlots = true;
	Bag.Id = FGuid::NewGuid();
	auto Primary = ExtractionItems::Make(TEXT("AK"));
	Primary.Slot = 0;
	Bag.Items.Add(Primary);
	auto Overflow = UExtractionInventoryComponent::GrantStartingSupply(Bag);
	TestTrue(TEXT("Primary preserved"), Bag.InSlot(0)->Id == Primary.Id);
	if (!TestNotNull(TEXT("Secondary equipped"), Bag.InSlot(1))) return false;
	TestEqual(TEXT("Secondary is SCAR"), Bag.InSlot(1)->Definition, FName(TEXT("SCARH")));
	TestEqual(TEXT("Full magazine"), Bag.InSlot(1)->Magazine, 20);
	TestEqual(TEXT("Reserve supplied"), Bag.Count(TEXT("Ammo76251")), 60);
	TestTrue(TEXT("Empty overflow"), Overflow.Items.IsEmpty());
	FString Error;
	TestTrue(TEXT("Inventory validates"), ExtractionItems::Validate(Bag, Error));

	const auto ExistingScar = *Bag.InSlot(1);
	Overflow = UExtractionInventoryComponent::GrantStartingSupply(Bag);
	TestEqual(TEXT("Existing SCAR is not duplicated"), Bag.Count(TEXT("SCARH")), 1);
	TestTrue(TEXT("Existing SCAR GUID preserved"), Bag.InSlot(1)->Id == ExistingScar.Id);
	TestEqual(TEXT("No ground SCAR when already equipped"), Overflow.Count(TEXT("SCARH")), 0);

	FExtractionContainer Full;
	Full.Id = FGuid::NewGuid();
	Full.WeaponSlots = true;
	Full.Width = Full.Height = 1;
	auto Secondary = ExtractionItems::Make(TEXT("AK"));
	Secondary.Slot = 1;
	Full.Items.Add(Secondary);
	ExtractionItems::AutoPlace(Full, ExtractionItems::Make(TEXT("Ammo76251"), 59));
	Overflow = UExtractionInventoryComponent::GrantStartingSupply(Full);
	TestTrue(TEXT("Occupied secondary preserved"), Full.InSlot(1)->Id == Secondary.Id);
	TestEqual(TEXT("One round fits remaining stack"), Full.Count(TEXT("Ammo76251")), 60);
	TestEqual(TEXT("Remaining ammo drops"), Overflow.Count(TEXT("Ammo76251")), 59);
	TestEqual(TEXT("Unfitted gun drops"), Overflow.Count(TEXT("SCARH")), 1);
	TestTrue(TEXT("Overflow validates"), ExtractionItems::Validate(Overflow, Error));

	const FString Path = FPaths::ProjectSavedDir() / TEXT("Extraction/StartingSupplyTests") / FGuid::NewGuid().
		ToString();
	auto* Store = NewObject<UExtractionPersistence>(NewObject<UGameInstance>());
	Store->LoadForTest(Path);
	FGuid Profile, Raid;
	FString Token;
	TestTrue(TEXT("Open profile"), Store->OpenProfile({}, {}, Profile, Token));
	TestTrue(TEXT("Begin raid"), Store->BeginRaid({Profile}, Raid));
	TestTrue(TEXT("Activate raid"), Store->ActivateRaid(Raid));
	FExtractionContainer Granted = Store->FindRaid(Raid)->Players[0].Original;
	UExtractionInventoryComponent::GrantStartingSupply(Granted);
	TestTrue(TEXT("Granted supplies settle normally"), Store->Settle(Raid, Profile, EExtractionOutcome::Extracted,
	                                                                 Granted));
	Store = NewObject<UExtractionPersistence>(NewObject<UGameInstance>());
	Store->LoadForTest(Path);
	TestEqual(TEXT("SCAR survives storage reload"), Store->View(Profile)->Stash.Count(TEXT("SCARH")), 1);
	TestEqual(TEXT("Reserve survives storage reload"), Store->View(Profile)->Stash.Count(TEXT("Ammo76251")), 60);
	return true;
}
#endif
