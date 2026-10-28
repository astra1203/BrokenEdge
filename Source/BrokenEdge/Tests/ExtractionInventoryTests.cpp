#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Extraction/ExtractionItems.h"

namespace
{
	FString Snapshot(const FExtractionContainer& C)
	{
		FString S = FString::Printf(TEXT("%s:%d:%d:%d:%d"), *C.Id.ToString(), C.Revision, C.Width, C.Height,
		                            C.WeaponSlots);
		for (const auto& I : C.Items)
			S += FString::Printf(TEXT("|%s,%s,%d,%d,%d,%d,%d,%d"), *I.Id.ToString(),
			                     *I.Definition.ToString(), I.Quantity, I.Cell.X, I.Cell.Y, I.Rotated, I.Slot,
			                     I.Magazine);
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionGridRules, "BrokenEdge.Extraction.Inventory.GridTransactions",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionGridRules::RunTest(const FString&)
{
	for (const auto Id : UExtractionItemDefinition::Ids())
		if (!TestNotNull(*Id.ToString(), UExtractionItemDefinition::Get(Id))) return false;
	FString Error;
	FExtractionContainer A, B;
	A.Id = FGuid::NewGuid();
	B.Id = FGuid::NewGuid();
	A.WeaponSlots = B.WeaponSlots = true;
	auto Gun = ExtractionItems::Make(TEXT("AK"));
	Gun.Magazine = 17;
	TestTrue(TEXT("Place a rifle"), ExtractionItems::AutoPlace(A, Gun));
	FExtractionItemRequest R;
	R.Item = Gun.Id;
	R.AutoPlace = false;
	R.Cell = FIntPoint(3, 0);
	R.Rotated = true;
	TestTrue(TEXT("Rotate a 4x2 rifle into 2x4 space"), ExtractionItems::Transfer(A, A, R, Error));
	TestEqual(TEXT("Rotation preserves identity"), A.Items[0].Id, Gun.Id);
	TestEqual(TEXT("Rotation preserves loaded rounds"), A.Items[0].Magazine, 17);
	const FString Before = Snapshot(A);
	R.Cell = FIntPoint(5, 0);
	TestFalse(TEXT("Reject out-of-bounds rotated rifle"), ExtractionItems::Transfer(A, A, R, Error));
	TestEqual(TEXT("Rejected rotation is byte-equivalent state"), Snapshot(A), Before);
	R.Slot = 0;
	TestTrue(TEXT("Equip rifle"), ExtractionItems::Transfer(A, A, R, Error));
	TestEqual(TEXT("Equipped identity retained"), A.InSlot(0)->Id, Gun.Id);
	R.Slot = -1;
	R.AutoPlace = true;
	TestTrue(TEXT("Transfer rifle into another container"), ExtractionItems::Transfer(A, B, R, Error));
	TestTrue(TEXT("Source relinquishes identity"), A.Find(Gun.Id) == nullptr);
	TestEqual(TEXT("Recipient keeps loaded rounds"), B.Find(Gun.Id)->Magazine, 17);
	const FString After = Snapshot(B);
	TestFalse(TEXT("Taking the same identity twice fails"), ExtractionItems::Transfer(A, B, R, Error));
	TestEqual(TEXT("Second take cannot change recipient"), Snapshot(B), After);
	TestTrue(TEXT("Valid destination grid"), ExtractionItems::Validate(B, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionStackRules, "BrokenEdge.Extraction.Inventory.StackConservation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionStackRules::RunTest(const FString&)
{
	FExtractionContainer A, B;
	FString Error;
	auto Ammo = ExtractionItems::Make(TEXT("Ammo"), 60);
	TestTrue(TEXT("Initial ammunition stack"), ExtractionItems::AutoPlace(A, Ammo));
	FExtractionItemRequest R;
	R.Item = Ammo.Id;
	R.Quantity = 20;
	R.AutoPlace = false;
	R.Cell = FIntPoint(1, 0);
	TestTrue(TEXT("Split in same container"), ExtractionItems::Transfer(A, A, R, Error));
	TestEqual(TEXT("Split conserves total"), A.Count(TEXT("Ammo")), 60);
	TestEqual(TEXT("Original identity retains remainder"), A.Find(Ammo.Id)->Quantity, 40);
	TestEqual(TEXT("Split creates second stack"), A.Items.Num(), 2);
	const FGuid Split = A.Items[1].Id;
	TestTrue(TEXT("Split identity is new"), Split != Ammo.Id);
	R.Item = Split;
	R.MergeInto = Ammo.Id;
	TestTrue(TEXT("Merge split back"), ExtractionItems::Transfer(A, A, R, Error));
	TestEqual(TEXT("Merge conserves total"), A.Count(TEXT("Ammo")), 60);
	TestEqual(TEXT("Destination identity retained"), A.Items[0].Id, Ammo.Id);
	TestNull(TEXT("Consumed split identity retired"), A.Find(Split));
	for (const int32 Invalid : {0, -1, 61, MAX_int32})
	{
		const FString Before = Snapshot(A);
		R.Item = Ammo.Id;
		R.Quantity = Invalid;
		R.MergeInto.Invalidate();
		TestFalse(TEXT("Reject invalid count"), ExtractionItems::Transfer(A, B, R, Error));
		TestEqual(TEXT("Invalid count cannot mutate source"), Snapshot(A), Before);
		TestEqual(TEXT("Invalid count cannot create destination items"), B.Items.Num(), 0);
	}
	// Auto-placement may tentatively merge one round, but must roll it back if
	// the rest cannot fit. This is the full-bag duplication/loss boundary.
	B.Width = 1;
	B.Height = 1;
	TestTrue(TEXT("Fill destination with 59 rounds"), ExtractionItems::AutoPlace(
		         B, ExtractionItems::Make(TEXT("Ammo"), 59)));
	const FString BeforeA = Snapshot(A), BeforeB = Snapshot(B);
	R.Item = Ammo.Id;
	R.Quantity = 60;
	R.AutoPlace = true;
	TestFalse(TEXT("Reject partial capacity"), ExtractionItems::Transfer(A, B, R, Error));
	TestEqual(TEXT("Capacity failure preserves source"), Snapshot(A), BeforeA);
	TestEqual(TEXT("Capacity failure rolls back tentative merge"), Snapshot(B), BeforeB);
	R.Quantity = 1;
	TestTrue(TEXT("Explicit partial quantity fills available stack"), ExtractionItems::Transfer(A, B, R, Error));
	TestEqual(TEXT("Transfer conserves 119 rounds"), A.Count(TEXT("Ammo")) + B.Count(TEXT("Ammo")), 119);
	TestEqual(TEXT("Full stack max enforced"), B.Items[0].Quantity, 60);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionEquipmentRules, "BrokenEdge.Extraction.Inventory.EquipmentAndValidation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionEquipmentRules::RunTest(const FString&)
{
	FExtractionContainer A, B;
	B.WeaponSlots = true;
	FString Error;
	auto Med = ExtractionItems::Make(TEXT("Medkit"));
	TestTrue(TEXT("Place medical item"), ExtractionItems::AutoPlace(A, Med));
	FExtractionItemRequest R;
	R.Item = Med.Id;
	R.Slot = 0;
	TestFalse(TEXT("Medical item cannot occupy weapon slot"), ExtractionItems::Transfer(A, B, R, Error));
	auto Gun = ExtractionItems::Make(TEXT("AK"));
	TestTrue(TEXT("Place rifle"), ExtractionItems::AutoPlace(A, Gun));
	R.Item = Gun.Id;
	TestTrue(TEXT("Equip first rifle"), ExtractionItems::Transfer(A, B, R, Error));
	auto Second = ExtractionItems::Make(TEXT("AK"));
	TestTrue(TEXT("Place another rifle"), ExtractionItems::AutoPlace(A, Second));
	R.Item = Second.Id;
	const FString BeforeA = Snapshot(A), BeforeB = Snapshot(B);
	TestFalse(TEXT("Occupied weapon slot cannot silently replace or drop"), ExtractionItems::Transfer(A, B, R, Error));
	TestEqual(TEXT("Occupied slot preserves both containers"), Snapshot(A) + Snapshot(B), BeforeA + BeforeB);
	R.Slot = 1;
	TestTrue(TEXT("Equip secondary"), ExtractionItems::Transfer(A, B, R, Error));
	const FExtractionItem Duplicate = B.Items[0];
	B.Items.Add(Duplicate);
	TestFalse(TEXT("Reject duplicated identity"), ExtractionItems::Validate(B, Error));
	B.Items.Pop();
	B.Items[0].Magazine = 31;
	TestFalse(TEXT("Reject invalid magazine state"), ExtractionItems::Validate(B, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionGearContainers, "BrokenEdge.Extraction.Inventory.GearContainers",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionGearContainers::RunTest(const FString&)
{
	FString Error;
	FExtractionContainer C;
	C.Id = FGuid::NewGuid();
	C.WeaponSlots = C.GearGrids = true;
	TestTrue(TEXT("No gear means no grid"), ExtractionItems::Grids(C).IsEmpty());
	TestFalse(TEXT("Nothing fits without a backpack"), ExtractionItems::AutoPlace(C, ExtractionItems::Make(TEXT("Medkit"))));
	for (const auto& [Slot, Definition] : {TPair<int32, FName>(ExtractionGear::RigSlot, TEXT("Rig_Ammo")),
	                                       TPair<int32, FName>(ExtractionGear::BagSlot, TEXT("Bag_Hike")),
	                                       TPair<int32, FName>(ExtractionGear::SafeSlot, TEXT("Safe_Basic"))})
	{
		auto Gear = ExtractionItems::Make(Definition);
		Gear.Slot = Slot;
		TestTrue(*FString::Printf(TEXT("Equip %s"), *Definition.ToString()), ExtractionItems::Fits(C, Gear));
		C.Items.Add(Gear);
	}
	auto Wrong = ExtractionItems::Make(TEXT("Bag_ALS"));
	Wrong.Slot = ExtractionGear::RigSlot;
	TestFalse(TEXT("Backpack cannot go into the rig slot"), ExtractionItems::Fits(C, Wrong));

	ExtractionItems::FGrid Rig, Bag, Safe;
	TestTrue(TEXT("Rig grid exists"), ExtractionItems::GridOf(C, ExtractionGear::RigGrid, Rig));
	TestTrue(TEXT("Backpack grid exists"), ExtractionItems::GridOf(C, ExtractionGear::BagGrid, Bag));
	TestTrue(TEXT("Safe grid exists"), ExtractionItems::GridOf(C, ExtractionGear::SafeGrid, Safe));
	// 10 格胸挂按 6 列排：第一行 6 格、第二行只有 4 格。
	TestEqual(TEXT("Rig keeps official cell count"), Rig.Cells, 10);
	TestEqual(TEXT("Rig has two rows"), Rig.Rows(), 2);
	TestTrue(TEXT("Partial row cell exists"), Rig.Has(3, 1));
	TestFalse(TEXT("Cells beyond the count do not exist"), Rig.Has(4, 1));
	TestEqual(TEXT("24-cell backpack equals the legacy 6x4"), Bag.Width * Bag.Rows(), 24);
	TestEqual(TEXT("Basic safe box is 2x2"), Safe.Cells, 4);

	auto Beyond = ExtractionItems::Make(TEXT("Medkit"));
	Beyond.Slot = ExtractionGear::RigGrid;
	Beyond.Cell = FIntPoint(5, 1);
	TestFalse(TEXT("Missing cell in the partial row rejects placement"), ExtractionItems::Fits(C, Beyond));

	TestTrue(TEXT("Auto place succeeds"), ExtractionItems::AutoPlace(C, ExtractionItems::Make(TEXT("Medkit"))));
	TestTrue(TEXT("Auto place fills the rig first"),
	         C.Items.ContainsByPredicate([](const auto& I)
	         {
		         return I.Definition == TEXT("Medkit") && I.Slot == ExtractionGear::RigGrid;
	         }));
	for (int32 N = 0; N < 60; ++N) ExtractionItems::AutoPlace(C, ExtractionItems::Make(TEXT("Electronics")));
	TestFalse(TEXT("Auto place never fills the safe box"),
	          C.Items.ContainsByPredicate([](const auto& I) { return I.Slot == ExtractionGear::SafeGrid; }));
	TestTrue(TEXT("Explicit safe box placement works"),
	         ExtractionItems::AutoPlace(C, ExtractionItems::Make(TEXT("Document")), ExtractionGear::SafeGrid));
	TestTrue(TEXT("Gear container validates"), ExtractionItems::Validate(C, Error));

	// 背包里有东西时不能取下，否则物品失去所在网格。
	FExtractionContainer Stash;
	Stash.Id = FGuid::NewGuid();
	Stash.Width = Stash.Height = 16;
	FExtractionItemRequest Take;
	Take.Item = C.InSlot(ExtractionGear::BagSlot)->Id;
	const FString Before = Snapshot(C);
	TestFalse(TEXT("Non-empty backpack cannot be removed"), ExtractionItems::Transfer(C, Stash, Take, Error));
	TestEqual(TEXT("Rejected removal changes nothing"), Snapshot(C), Before);

	// 死亡只保留安全箱本体与其中物品。
	int32 Kept = 0;
	for (const auto& I : C.Items) Kept += ExtractionItems::Protected(I);
	TestEqual(TEXT("Safe box and its document survive death"), Kept, 2);
	return true;
}
#endif
