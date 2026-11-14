#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Extraction/World/ExtractionLootLayout.h"

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
	// 不硬编码数量：以数据本身为基准，避免每次调点位都要改测试。
	// 每局 = 全部固定点（启用、非散落、无组）+ 每个随机组恰好一个（7F 新区域按组每局刷一个）。
	TSet<FName> Fixed;
	TMap<FName, TSet<FName>> Groups;
	for (const auto& P : Authored->Points)
	{
		if (!P.Enabled || P.Kind == UExtractionLootLayout::LooseKind) continue;
		// Spawn supports static container meshes as well as the original skeletal meshes.
		// Validate each candidate once, before the 1000-seed coverage loop.
		if (!TestTrue(*FString::Printf(TEXT("Source model configured at %s"), *P.Id.ToString()),
			P.StaticAppearance != nullptr || P.Appearance != nullptr)) return false;
		if (!TestNotNull(*FString::Printf(TEXT("Project recipe configured at %s"), *P.Id.ToString()), P.Recipe.Get())) return false;
		if (P.Group.IsNone()) Fixed.Add(P.Id);
		else Groups.FindOrAdd(P.Group).Add(P.Id);
	}
	int32 Candidates = 0;
	for (const auto& Group : Groups) Candidates += Group.Value.Num();
	TestTrue(TEXT("Production layout has fixed containers"), Fixed.Num() > 0);
	for (int32 Seed = 0; Seed < 1000; ++Seed)
	{
		if (!TestTrue(TEXT("Production layout selects"), Authored->Select(Seed, Out, Error))) return false;
		TestEqual(TEXT("Fixed containers plus one per group"), Out.Num(), Fixed.Num() + Groups.Num());
		int32 FixedHits = 0;
		TMap<FName, int32> GroupHits;
		for (int32 Index : Out)
		{
			const auto& P = Authored->Points[Index];
			Seen.Add(P.Id);
			if (P.Group.IsNone()) ++FixedHits;
			else ++GroupHits.FindOrAdd(P.Group);
			TestTrue(TEXT("Selected point is enabled"), P.Enabled);
			if (P.Kind == UExtractionLootLayout::LooseKind)
				AddError(TEXT("A loose site occupied an authored container slot"));
		}
		TestEqual(TEXT("Every fixed container is selected"), FixedHits, Fixed.Num());
		TestEqual(TEXT("Every group spawns"), GroupHits.Num(), Groups.Num());
		for (const auto& Hit : GroupHits) TestEqual(TEXT("Exactly one container per group"), Hit.Value, 1);
	}
	TestEqual(TEXT("Every production container covered"), Seen.Num(), Fixed.Num() + Candidates);
	return true;
}
#endif
