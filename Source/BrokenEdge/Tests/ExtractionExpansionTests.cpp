#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Templates/UnrealTemplate.h"
#include "BrokenEdge/Extraction/ExtractionItems.h"
#include "BrokenEdge/Extraction/ExtractionMapDefinition.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionWeaponDefinitionTest,
                                 "BrokenEdge.Extraction.Expansion.WeaponDefinitionTransactions",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionWeaponDefinitionTest::RunTest(const FString&)
{
	auto* Def = const_cast<UExtractionItemDefinition*>(UExtractionItemDefinition::Get(TEXT("AK")));
	if (!TestNotNull(TEXT("Existing AK definition remains available"), Def)) return false;
	// Override loaded definition transiently, restoring every value before returning.
	TGuardValue<int32> Capacity(Def->MagazineCapacity, 20);
	TGuardValue<TArray<FName>> Ammo(Def->CompatibleAmmo, {TEXT("Ammo")});
	TGuardValue<TArray<FName>> Parts(Def->CompatibleAttachments, {TEXT("RedDot")});
	auto Gun = ExtractionItems::Make(TEXT("AK"));
	FString Error;
	TestEqual(TEXT("Factory follows configured magazine capacity"), Gun.Magazine, 20);
	TestTrue(TEXT("Configured magazine and provenance validate"), ExtractionItems::ValidateItem(Gun, true, Error));
	Gun.Magazine = 21;
	ExtractionItems::SeedOrigins(Gun);
	TestFalse(TEXT("Over-capacity magazine rejected"), ExtractionItems::ValidateItem(Gun, true, Error));
	Gun.Magazine = 0;
	Gun.MagazineOrigins.Reset();
	FExtractionContainer Bag;
	Bag.Id = FGuid::NewGuid();
	Bag.Items.Add(Gun);
	auto Grip = ExtractionItems::Make(TEXT("VerticalGrip"));
	Grip.Cell = FIntPoint(4, 0);
	Bag.Items.Add(Grip);
	const int32 Revision = Bag.Revision;
	FExtractionItemRequest Request;
	Request.Op = EExtractionItemOp::Attach;
	Request.Item = Grip.Id;
	Request.Weapon = Gun.Id;
	TestFalse(TEXT("Incompatible attachment rejected by transaction"), ExtractionItems::Configure(Bag, Request, Error));
	TestNotNull(TEXT("Rejected attachment remains in bag"), Bag.Find(Grip.Id));
	TestEqual(TEXT("Rejected transaction does not advance revision"), Bag.Revision, Revision);
	Request.Op = EExtractionItemOp::SelectAmmo;
	Request.Item = Gun.Id;
	Request.AmmoDefinition = TEXT("AmmoAP");
	TestFalse(TEXT("Other caliber/type cannot be selected"), ExtractionItems::Configure(Bag, Request, Error));
	TestEqual(TEXT("Rejected ammo selection preserves original type"), Bag.Find(Gun.Id)->MagazineAmmo,
	          FName(TEXT("Ammo")));
	TestTrue(TEXT("Details follow configured capacity"), ExtractionItems::Details(Gun).Contains(TEXT("/20")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionExitDefinitionTest,
                                 "BrokenEdge.Extraction.Expansion.ExitConfiguration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionExitDefinitionTest::RunTest(const FString&)
{
	FExtractionExitDefinition Exit;
	TestFalse(TEXT("Unnamed point cannot be enabled"), Exit.IsValid());
	Exit.Id = TEXT("Fixture");
	TestTrue(TEXT("Named legacy bounds and timing valid"), Exit.IsValid());
	Exit.HoldSeconds = 0;
	TestFalse(TEXT("Instant extraction rejected"), Exit.IsValid());
	Exit.HoldSeconds = 10;
	Exit.OpensAfterSeconds = 60;
	Exit.ClosesAfterSeconds = 60;
	TestFalse(TEXT("Empty opening window rejected"), Exit.IsValid());
	Exit.ClosesAfterSeconds = 120;
	TestTrue(TEXT("Finite opening window accepted"), Exit.IsValid());
	Exit.Capacity = -1;
	TestFalse(TEXT("Negative capacity rejected"), Exit.IsValid());
	Exit.Capacity = 1;
	Exit.HalfExtent.X = 0;
	TestFalse(TEXT("Empty volume rejected"), Exit.IsValid());
	return true;
}
#endif
