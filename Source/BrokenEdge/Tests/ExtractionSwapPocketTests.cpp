#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Extraction/ExtractionItems.h"
#include "JsonObjectConverter.h"

namespace
{
	FString SnapshotInventory(const FExtractionContainer& C)
	{
		FString Result;
		FJsonObjectConverter::UStructToJsonObjectString(C, Result);
		return Result;
	}

	FExtractionItem At(FName Definition, int32 X, int32 Y, int32 Slot = -1)
	{
		auto I = ExtractionItems::Make(Definition);
		I.Cell = FIntPoint(X, Y);
		I.Slot = Slot;
		return I;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionSwapPocketTest, "BrokenEdge.Extraction.Inventory.SwapAndPockets",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionSwapPocketTest::RunTest(const FString&)
{
	FString Error;
	FExtractionContainer C;
	C.Id = FGuid::NewGuid();
	C.Width = 6;
	C.Height = 2;
	auto Ammo = At(TEXT("Ammo"), 5, 0);
	auto Tool = At(TEXT("Tools"), 0, 0);
	auto Block = At(TEXT("Bandage"), 5, 1);
	C.Items = {Ammo, Tool, Block};
	FExtractionItemRequest R;
	R.Op = EExtractionItemOp::Swap;
	R.Item = Ammo.Id;
	R.SwapWith = Tool.Id;
	R.Quantity = 1;
	R.Slot = -1;
	R.AutoPlace = false;
	TestTrue(
		TEXT("Swap finds a different source cell when original position cannot fit"),
		ExtractionItems::Transfer(C, C, R, Error));
	TestEqual(TEXT("Moving item snaps to target origin"), C.Find(Ammo.Id)->Cell, Tool.Cell);
	TestEqual(TEXT("Displaced item uses first fitting source cell"), C.Find(Tool.Id)->Cell, FIntPoint(1, 0));
	TestEqual(TEXT("Third item is never rearranged"), C.Find(Block.Id)->Cell, Block.Cell);
	TestTrue(TEXT("Swap preserves origin lot"), C.Find(Tool.Id)->Origins[0].Lot == Tool.Origins[0].Lot);
	TestEqual(TEXT("One revision per atomic swap"), C.Revision, 2);

	FExtractionContainer Full;
	Full.Id = FGuid::NewGuid();
	Full.Width = 6;
	Full.Height = 1;
	Full.Items = {Ammo, Tool};
	for (int32 X = 2; X < 5; ++X) Full.Items.Add(At(TEXT("Bandage"), X, 0));
	const FString Before = SnapshotInventory(Full);
	TestFalse(TEXT("Fragmented source rejects swap"), ExtractionItems::Transfer(Full, Full, R, Error));
	TestEqual(TEXT("Failed swap preserves complete state"), SnapshotInventory(Full), Before);

	FExtractionContainer A, B;
	A.Id = FGuid::NewGuid();
	B.Id = FGuid::NewGuid();
	A.Items = {At(TEXT("BoltSniper"), 0, 0)};
	B.Items = {At(TEXT("PumpShotgun"), 0, 0)};
	const auto Sniper = A.Items[0], Shotgun = B.Items[0];
	R.Item = Sniper.Id;
	R.SwapWith = Shotgun.Id;
	TestTrue(TEXT("Cross-container long guns swap atomically"), ExtractionItems::Transfer(A, B, R, Error));
	TestTrue(
		TEXT("Both gun identities move"),
		A.Find(Shotgun.Id) && B.Find(Sniper.Id) && !A.Find(Sniper.Id) && !B.Find(Shotgun.Id));
	TestEqual(TEXT("Loaded magazine preserved"), B.Find(Sniper.Id)->Magazine, Sniper.Magazine);
	TestTrue(
		TEXT("Magazine provenance preserved"),
		B.Find(Sniper.Id)->MagazineOrigins[0].Lot == Sniper.MagazineOrigins[0].Lot);
	TestFalse(TEXT("Retry cannot duplicate a swapped identity"), ExtractionItems::Transfer(A, B, R, Error));

	FExtractionContainer Merge;
	auto S = ExtractionItems::Make(TEXT("Ammo"), 50);
	auto T = ExtractionItems::Make(TEXT("Ammo"), 45);
	T.Cell = FIntPoint(1, 0);
	Merge.Items = {S, T};
	R = FExtractionItemRequest();
	R.Item = S.Id;
	R.MergeInto = T.Id;
	R.Quantity = 50;
	TestTrue(TEXT("Partial stack merge succeeds"), ExtractionItems::Transfer(Merge, Merge, R, Error));
	TestEqual(TEXT("Target fills to limit"), Merge.Find(T.Id)->Quantity, 60);
	TestEqual(TEXT("Remainder stays in source"), Merge.Find(S.Id)->Quantity, 35);
	TestEqual(TEXT("Remainder position retained"), Merge.Find(S.Id)->Cell, S.Cell);
	TestEqual(TEXT("Source provenance follows remainder"), Merge.Find(S.Id)->Origins[0].Quantity, 35);
	TestEqual(TEXT("Merged quantity conserved"), Merge.Count(TEXT("Ammo")), 95);

	FExtractionContainer Pockets;
	Pockets.GearGrids = Pockets.WeaponSlots = Pockets.PersonalPockets = true;
	ExtractionItems::FGrid Grid;
	TestTrue(TEXT("Pockets exist without a bag or rig"), ExtractionItems::GridOf(
		         Pockets, ExtractionGear::PocketGrid, Grid));
	TestEqual(TEXT("Five independent pockets"), Grid.Pockets.Num(), 5);
	TestFalse(TEXT("Adjacent pockets cannot hold a two-cell item"), Grid.Holds(FIntPoint(0, 0), FIntPoint(2, 1)));
	for (int32 X = 0; X < 5; ++X)
	{
		auto I = At(TEXT("FragGrenade"), X, 0, ExtractionGear::PocketGrid);
		TestTrue(TEXT("Each pocket accepts a one-cell item"), ExtractionItems::Fits(Pockets, I));
		Pockets.Items.Add(I);
		TestFalse(TEXT("Pocket contents are not death protected"), ExtractionItems::Protected(I));
	}
	TestTrue(TEXT("Pocket container validates"), ExtractionItems::Validate(Pockets, Error));
	TestFalse(TEXT("Sixth item cannot fit"), ExtractionItems::AutoPlace(
		          Pockets, ExtractionItems::Make(TEXT("FragGrenade"))));
	FExtractionContainer Bundle;
	Bundle.GearGrids = true;
	TestFalse(
		TEXT("Gear bundle has no personal pockets"), ExtractionItems::GridOf(Bundle, ExtractionGear::PocketGrid, Grid));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionSwapSlotTest, "BrokenEdge.Extraction.Inventory.SwapEquipmentSlots",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionSwapSlotTest::RunTest(const FString&)
{
	FString Error;
	// 拖动 Item 松手在 With 上：界面发出的交换请求。
	auto SwapRequest = [](const FExtractionItem& Item, const FExtractionItem& With)
	{
		FExtractionItemRequest R;
		R.Op = EExtractionItemOp::Swap;
		R.Item = Item.Id;
		R.SwapWith = With.Id;
		R.Quantity = Item.Quantity;
		R.Slot = With.Slot;
		R.Cell = With.Cell;
		R.AutoPlace = false;
		return R;
	};
	FExtractionContainer Stash, Loadout;
	Stash.Id = FGuid::NewGuid();
	Stash.Width = 10;
	Stash.Height = 12;
	Loadout.Id = FGuid::NewGuid();
	Loadout.WeaponSlots = Loadout.GearGrids = true;
	const auto Spare = At(TEXT("PumpShotgun"), 2, 3);
	Stash.Items = {Spare};
	auto Held = ExtractionItems::Make(TEXT("BoltSniper"));
	Held.Slot = 0;
	Loadout.Items = {Held};

	TestTrue(TEXT("Grid gun swaps into an occupied weapon slot"),
	         ExtractionItems::Transfer(Stash, Loadout, SwapRequest(Spare, Held), Error));
	TestTrue(TEXT("Dragged gun now holds the slot"), Loadout.InSlot(0) && Loadout.InSlot(0)->Id == Spare.Id);
	const auto* Back = Stash.Find(Held.Id);
	TestTrue(TEXT("Replaced gun goes back to the stash grid"), Back && Back->Slot == -1);
	TestTrue(TEXT("Stash still validates"), ExtractionItems::Validate(Stash, Error));

	auto Secondary = ExtractionItems::Make(TEXT("AK"));
	Secondary.Slot = 1;
	Loadout.Items.Add(Secondary);
	TestTrue(TEXT("Primary and secondary weapons swap"),
	         ExtractionItems::Transfer(Loadout, Loadout, SwapRequest(*Loadout.InSlot(0), Secondary), Error));
	TestTrue(TEXT("Secondary moved to primary"), Loadout.InSlot(0) && Loadout.InSlot(0)->Id == Secondary.Id);
	TestTrue(TEXT("Primary moved to secondary"), Loadout.InSlot(1) && Loadout.InSlot(1)->Id == Spare.Id);

	const auto Bandage = At(TEXT("Bandage"), 0, 0);
	Stash.Items.Add(Bandage);
	const FString StashBefore = SnapshotInventory(Stash), LoadoutBefore = SnapshotInventory(Loadout);
	TestFalse(TEXT("A bandage cannot take a weapon slot"),
	          ExtractionItems::Transfer(Stash, Loadout, SwapRequest(Bandage, *Loadout.InSlot(0)), Error));
	TestFalse(TEXT("A slotted gun cannot hand its slot to a bandage"),
	          ExtractionItems::Transfer(Loadout, Stash, SwapRequest(*Loadout.InSlot(0), Bandage), Error));
	TestEqual(TEXT("Failed slot swaps leave the stash unchanged"), SnapshotInventory(Stash), StashBefore);
	TestEqual(TEXT("Failed slot swaps leave the loadout unchanged"), SnapshotInventory(Loadout), LoadoutBefore);

	auto Pack = ExtractionItems::Make(TEXT("Bag_Hike"));
	Pack.Slot = ExtractionGear::BagSlot;
	Loadout.Items.Add(Pack);
	TestTrue(TEXT("Pack holds an item"),
	         ExtractionItems::AutoPlace(Loadout, ExtractionItems::Make(TEXT("Bandage")), ExtractionGear::BagGrid));
	TestTrue(TEXT("Spare pack fits the stash"), ExtractionItems::AutoPlace(Stash, ExtractionItems::Make(TEXT("Bag_Test"))));
	const FExtractionItem* SparePack = Stash.Items.FindByPredicate([](const FExtractionItem& I)
	{
		return I.Definition == TEXT("Bag_Test");
	});
	if (!TestNotNull(TEXT("Spare pack placed"), SparePack)) return false;
	TestFalse(TEXT("A packed bag cannot be swapped off"),
	          ExtractionItems::Transfer(Stash, Loadout, SwapRequest(*SparePack, Pack), Error));
	TestEqual(TEXT("Packed bag swap explains why"), Error, FString(TEXT("请先清空这件装备里的物品")));
	return true;
}
#endif
