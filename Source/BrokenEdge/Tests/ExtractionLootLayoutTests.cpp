#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Extraction/ExtractionLootLayout.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionLootLayoutTest, "BrokenEdge.Extraction.LootLayout.Selection",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExtractionLootLayoutTest::RunTest(const FString&)
{
	auto* Layout = NewObject<UExtractionLootLayout>();
	for (int32 I = 0; I < 6; ++I)
	{
		FExtractionLootPoint P;
		P.Id = FName(*FString::Printf(TEXT("Point%d"), I));
		P.Group = I < 2 ? FName(TEXT("TwoCandidates")) : I < 5 ? FName(TEXT("ThreeCandidates")) : NAME_None;
		Layout->Points.Add(P);
	}
	TSet<FName> Seen;
	for (int32 Seed = 0; Seed < 1000; ++Seed)
	{
		TArray<int32> A, B;
		FString Error;
		if (!TestTrue(TEXT("Valid selection"), Layout->Select(Seed, A, Error))) return false;
		Layout->Select(Seed, B, Error);
		TestTrue(TEXT("Replay seed selects same points"), A == B);
		TestEqual(TEXT("One fixed point and one per candidate group"), A.Num(), 3);
		int32 Two = 0, Three = 0;
		for (int32 I : A)
		{
			Seen.Add(Layout->Points[I].Id);
			Two += I < 2;
			Three += I >= 2 && I < 5;
		}
		TestEqual(TEXT("Two-way group exclusivity"), Two, 1);
		TestEqual(TEXT("Three-way group exclusivity"), Three, 1);
	}
	TestEqual(TEXT("Every legal candidate exercised"), Seen.Num(), 6);
	TArray<int32> Out;
	FString Error;
	Layout->Points[0].Enabled = false;
	Layout->Select(3, Out, Error);
	TestFalse(TEXT("Disabled candidate cannot spawn"), Out.Contains(0));
	Layout->Points[1].Id = Layout->Points[0].Id;
	TestFalse(TEXT("Duplicate identity rejected atomically"), Layout->Select(3, Out, Error));
	TestTrue(TEXT("No partial selection on failure"), Out.IsEmpty());
	const auto* Authored = LoadObject<UExtractionLootLayout>(nullptr,
	                                                         TEXT(
		                                                         "/Game/Extraction/Loot/DA_SpaceCenter_Ma1_Loot.DA_SpaceCenter_Ma1_Loot"));
	if (!TestNotNull(TEXT("Production source layout exists"), Authored)) return false;
	Seen.Reset();
	for (int32 Seed = 0; Seed < 1000; ++Seed)
	{
		if (!TestTrue(TEXT("Production layout selects"), Authored->Select(Seed, Out, Error))) return false;
		TestEqual(TEXT("Production raid has five fixed and two grouped safes"), Out.Num(), 7);
		for (int32 Index : Out)
		{
			const auto& P = Authored->Points[Index];
			Seen.Add(P.Id);
			TestNotNull(TEXT("Source model configured"), P.Appearance.Get());
			TestNotNull(TEXT("Separate project recipe configured"), P.Recipe.Get());
		}
	}
	TestEqual(TEXT("Every production candidate covered"), Seen.Num(), 10);
	return true;
}
#endif
