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
#endif
