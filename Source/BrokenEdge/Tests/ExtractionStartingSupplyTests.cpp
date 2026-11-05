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
	if (!TestNotNull(TEXT("Primary equipped"), Bag.InSlot(0))) return false;
	TestEqual(TEXT("Primary is M4A1"), Bag.InSlot(0)->Definition, FName(TEXT("M4A1")));
	TestEqual(TEXT("Replaced AK kept in grid"), Bag.Count(TEXT("AK")), 1);
	if (!TestNotNull(TEXT("Secondary equipped"), Bag.InSlot(1))) return false;
	TestEqual(TEXT("Secondary is SCAR"), Bag.InSlot(1)->Definition, FName(TEXT("SCARH")));
	TestEqual(TEXT("Full magazine"), Bag.InSlot(1)->Magazine, 20);
	TestEqual(TEXT("SCAR reserve supplied"), Bag.Count(TEXT("Ammo76251")), 60);
	TestEqual(TEXT("M4 reserve supplied"), Bag.Count(TEXT("Ammo55645")), 60);
	for (int32 Slot : {2, 3, 4, 5})
		if (!TestNotNull(TEXT("Required protective gear equipped"), Bag.InSlot(Slot))) return false;
	TestEqual(TEXT("Helmet equipped"), Bag.InSlot(2)->Definition, FName(TEXT("Helmet2")));
	TestEqual(TEXT("Vest equipped"), Bag.InSlot(3)->Definition, FName(TEXT("Vest2")));
	TestEqual(TEXT("Large backpack equipped"), Bag.InSlot(5)->Definition, FName(TEXT("Bag_Test")));
	ExtractionItems::FGrid Grid;
	TestTrue(TEXT("Backpack grid exists"), ExtractionItems::GridOf(Bag, ExtractionGear::BagGrid, Grid));
	TestEqual(TEXT("Backpack width"), Grid.Width, 8);
	TestEqual(TEXT("Backpack cells"), Grid.Cells, 64);
	for (FName Gun : {TEXT("BoltSniper"), TEXT("PumpShotgun")})
	{
		const auto* Item = Bag.Items.FindByPredicate([&](const FExtractionItem& I) { return I.Definition == Gun; });
		if (!TestNotNull(TEXT("Test gun supplied"), Item)) return false;
		TestEqual(TEXT("Test gun inside backpack"), Item->Slot, ExtractionGear::BagGrid);
		TestEqual(TEXT("New test gun loaded"), Item->Magazine, UExtractionItemDefinition::Get(Gun)->MagazineCapacity);
	}
	TestEqual(TEXT("Sniper reserve"), Bag.Count(TEXT("Ammo_338")), 20);
	TestEqual(TEXT("Shotgun reserve"), Bag.Count(TEXT("Ammo_12G")), 24);
	TestEqual(TEXT("Two starting frag grenades"), Bag.Count(TEXT("FragGrenade")), 2);
	TestTrue(TEXT("Personal pockets enabled"), Bag.PersonalPockets);
	TestTrue(TEXT("Empty overflow"), Overflow.Items.IsEmpty());
	FString Error;
	TestTrue(TEXT("Inventory validates"), ExtractionItems::Validate(Bag, Error));

	const auto ExistingScar = *Bag.InSlot(1);
	TSet<FGuid> BeforeIds;
	for (const auto& I : Bag.Items) BeforeIds.Add(I.Id);
	Overflow = UExtractionInventoryComponent::GrantStartingSupply(Bag);
	TestEqual(TEXT("Existing SCAR is not duplicated"), Bag.Count(TEXT("SCARH")), 1);
	TestEqual(TEXT("Existing M4 is not duplicated"), Bag.Count(TEXT("M4A1")), 1);
	TestTrue(TEXT("Existing SCAR GUID preserved"), Bag.InSlot(1)->Id == ExistingScar.Id);
	TestEqual(TEXT("No ground SCAR when already equipped"), Overflow.Count(TEXT("SCARH")), 0);
	TSet<FGuid> AfterIds;
	for (const auto& I : Bag.Items) AfterIds.Add(I.Id);
	TestTrue(
		TEXT("Repeated grant keeps every item identity"),
		BeforeIds.Difference(AfterIds).IsEmpty() && AfterIds.Difference(BeforeIds).IsEmpty());
	TestEqual(TEXT("Repeated grant does not add reserve"), Bag.Count(TEXT("Ammo55645")), 60);
	TestEqual(TEXT("Repeated grant does not add sniper"), Bag.Count(TEXT("BoltSniper")), 1);
	TestEqual(TEXT("Repeated grant does not add grenades"), Bag.Count(TEXT("FragGrenade")), 2);
	FExtractionContainer ProtectedKit;
	ProtectedKit.Id = FGuid::NewGuid();
	ProtectedKit.GearGrids = ProtectedKit.WeaponSlots = true;
	auto GrenadeSafe = ExtractionItems::Make(TEXT("Safe_Field"));
	GrenadeSafe.Slot = ExtractionGear::SafeSlot;
	ProtectedKit.Items.Add(GrenadeSafe);
	auto ProtectedGrenade = ExtractionItems::Make(TEXT("FragGrenade"));
	ProtectedGrenade.Slot = ExtractionGear::SafeGrid;
	ProtectedKit.Items.Add(ProtectedGrenade);
	UExtractionInventoryComponent::GrantStartingSupply(ProtectedKit);
	int32 UsableGrenades = 0;
	for (const auto& I : ProtectedKit.Items)
		if (I.Definition == TEXT("FragGrenade") && I.Slot != ExtractionGear::SafeGrid) UsableGrenades += I.Quantity;
	TestEqual(TEXT("ProtectedKit storage does not consume the two usable test grenades"), UsableGrenades, 2);
	TestNotNull(TEXT("GrenadeSafe grenade identity is preserved"), ProtectedKit.Find(ProtectedGrenade.Id));

	FExtractionContainer Full;
	Full.Id = FGuid::NewGuid();
	Full.WeaponSlots = Full.GearGrids = true;
	auto Pack = ExtractionItems::Make(TEXT("Bag_Test"));
	Pack.Slot = ExtractionGear::BagSlot;
	Full.Items.Add(Pack);
	auto Secondary = ExtractionItems::Make(TEXT("AK"));
	Secondary.Slot = 1;
	Full.Items.Add(Secondary);
	for (int32 I = 0; I < 64; ++I)
		TestTrue(TEXT("Fill original backpack"), ExtractionItems::AutoPlace(
			         Full, ExtractionItems::Make(TEXT("Ammo"), 60)));
	Overflow = UExtractionInventoryComponent::GrantStartingSupply(Full);
	if (!TestNotNull(TEXT("Full pack still equips secondary"), Full.InSlot(1))) return false;
	TestEqual(TEXT("SCAR replaces occupied secondary"), Full.InSlot(1)->Definition, FName(TEXT("SCARH")));
	TestEqual(TEXT("Original AK preserved exactly once"), Full.Count(TEXT("AK")) + Overflow.Count(TEXT("AK")), 1);
	TestEqual(
		TEXT("Original ammo conserved across pack and overflow"),
		Full.Count(TEXT("Ammo")) + Overflow.Count(TEXT("Ammo")), 3840);
	TestFalse(TEXT("Full pack safely overflows old items"), Overflow.Items.IsEmpty());
	TestEqual(TEXT("Required sniper never overflows"), Full.Count(TEXT("BoltSniper")), 1);
	TestEqual(TEXT("Required shotgun never overflows"), Full.Count(TEXT("PumpShotgun")), 1);
	TestTrue(TEXT("Full inventory validates after repacking"), ExtractionItems::Validate(Full, Error));
	TestTrue(TEXT("Overflow validates"), ExtractionItems::Validate(Overflow, Error));
	FExtractionContainer Worn;
	Worn.Id = FGuid::NewGuid();
	Worn.GearGrids = Worn.WeaponSlots = true;
	auto OldPack = ExtractionItems::Make(TEXT("Bag_Hike"));
	OldPack.Slot = 5;
	Worn.Items.Add(OldPack);
	auto Helmet = ExtractionItems::Make(TEXT("Helmet3"));
	Helmet.Slot = 2;
	Helmet.Durability = 20;
	Worn.Items.Add(Helmet);
	auto Vest = ExtractionItems::Make(TEXT("Vest2"));
	Vest.Slot = 3;
	Vest.Durability = 0;
	Worn.Items.Add(Vest);
	auto Safe = ExtractionItems::Make(TEXT("Safe_Field"));
	Safe.Slot = 6;
	Worn.Items.Add(Safe);
	auto Protected = ExtractionItems::Make(TEXT("Bandage"));
	Protected.Slot = ExtractionGear::SafeGrid;
	Worn.Items.Add(Protected);
	auto ExistingSniper = ExtractionItems::Make(TEXT("BoltSniper"));
	ExistingSniper.Slot = 0;
	ExistingSniper.Magazine = 2;
	ExistingSniper.MagazineOrigins[0].Quantity = 2;
	Worn.Items.Add(ExistingSniper);
	Overflow = UExtractionInventoryComponent::GrantStartingSupply(Worn);
	if (!TestNotNull(TEXT("Old account has helmet"), Worn.InSlot(2)) ||
		!TestNotNull(TEXT("Old account has vest"), Worn.InSlot(3)))
		return false;
	TestTrue(
		TEXT("Usable higher-tier helmet retained"),
		Worn.InSlot(2)->Id == Helmet.Id && Worn.InSlot(2)->Durability == 20);
	TestTrue(
		TEXT("Broken vest replaced with usable armor"),
		Worn.InSlot(3)->Id != Vest.Id && Worn.InSlot(3)->Durability > 0);
	TestTrue(TEXT("Broken armor preserved"), Worn.Find(Vest.Id) || Overflow.Find(Vest.Id));
	TestTrue(TEXT("Replaced backpack preserved"), Worn.Find(OldPack.Id) || Overflow.Find(OldPack.Id));
	TestTrue(
		TEXT("Safe contents stay protected"),
		Worn.Find(Protected.Id) && Worn.Find(Protected.Id)->Slot == ExtractionGear::SafeGrid);
	TestTrue(
		TEXT("Existing sniper GUID and magazine retained"),
		Worn.Find(ExistingSniper.Id) && Worn.Find(ExistingSniper.Id)->Magazine == 2);
	TestTrue(TEXT("Old account loadout validates"), ExtractionItems::Validate(Worn, Error));
	FExtractionItemRequest PutAway;
	PutAway.Item = Worn.InSlot(0)->Id;
	PutAway.Slot = ExtractionGear::BagGrid;
	PutAway.AutoPlace = true;
	TestTrue(TEXT("Reserved space accepts the equipped rifle"), ExtractionItems::Transfer(Worn, Worn, PutAway, Error));

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
	TestEqual(TEXT("M4 survives storage reload"), Store->View(Profile)->Stash.Count(TEXT("M4A1")), 1);
	TestEqual(TEXT("Reserve survives storage reload"), Store->View(Profile)->Stash.Count(TEXT("Ammo76251")), 60);
	TestEqual(TEXT("Test sniper survives storage reload"), Store->View(Profile)->Stash.Count(TEXT("BoltSniper")), 1);
	TestEqual(TEXT("Test shotgun survives storage reload"), Store->View(Profile)->Stash.Count(TEXT("PumpShotgun")), 1);
	return true;
}
#endif
