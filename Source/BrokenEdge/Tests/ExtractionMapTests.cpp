#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Templates/UnrealTemplate.h"
#include "BrokenEdge/Extraction/Map/ExtractionMapRegistry.h"
#include "BrokenEdge/Extraction/World/ExtractionLootLayout.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionMapRegistryTest, "BrokenEdge.Extraction.Maps.Registry",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionMapRegistryTest::RunTest(const FString&)
{
	const auto* Registry = GetDefault<UExtractionMapRegistry>();
	FString Error;
	TestNull(TEXT("Unknown IDs cannot select arbitrary packages"), Registry->Find(TEXT("/Game/Maps/Lobby")));
	TestFalse(TEXT("Unknown ID cannot start a raid"), Registry->Validate(TEXT("NotRegistered"), Error));
	TestFalse(TEXT("Error is actionable"), Error.IsEmpty());
	for (FName Id : {FName(TEXT("SpaceCenter")), FName(TEXT("SovietMilitary"))})
	{
		TestTrue(*FString::Printf(TEXT("Valid registered map %s: %s"), *Id.ToString(), *Error),
		         Registry->Validate(Id, Error));
		auto* D = Registry->Find(Id);
		if (!TestNotNull(TEXT("Definition resolves"), D)) continue;
		TestFalse(TEXT("Localized map title"), D->DisplayName.IsEmpty());
		TestFalse(TEXT("Explicit world"), D->Level.IsNull());
		const auto Original = D->Exits;
		{
			TGuardValue<TArray<FExtractionExitDefinition>> Invalid(D->Exits, {});
			TestFalse(TEXT("Missing exits rejected before inventory deduction"), Registry->Validate(Id, Error));
		}
		TestEqual(TEXT("Fixture restored exits"), D->Exits.Num(), Original.Num());
	}
	if (const auto* D = Registry->Find(TEXT("SovietMilitary")))
	{
		const auto* DoorMesh = LoadObject<
			UStaticMesh>(nullptr, TEXT("/Game/UVB80/Assets/UVB80/Buildings/Door/SM_Door"));
		if (TestNotNull(TEXT("Military door mesh loads"), DoorMesh) && TestNotNull(
			TEXT("Military door body exists"), DoorMesh->GetBodySetup()))
		{
			const auto* Body = DoorMesh->GetBodySetup();
			TestEqual(TEXT("Moving door uses authored simple collision"), Body->CollisionTraceFlag.GetValue(),
			          CTF_UseSimpleAsComplex);
			TestEqual(TEXT("Moving door has a real collision box"), Body->AggGeom.BoxElems.Num(), 1);
			if (Body->AggGeom.BoxElems.Num() == 1)
			{
				const auto& Box = Body->AggGeom.BoxElems[0];
				TestTrue(TEXT("Door box has nonzero dimensions"), Box.X > 0 && Box.Y > 0 && Box.Z > 0);
				TestTrue(
					TEXT("Door box follows off-center hinge geometry"),
					Box.Center.Equals(DoorMesh->GetBoundingBox().GetCenter(), .1));
			}
		}
		TestEqual(TEXT("Four squad spawn candidates"), D->SpawnGroups.Num(), 4);
		TestEqual(TEXT("Three extraction zones"), D->Exits.Num(), 3);
		TestEqual(TEXT("Six guards"), D->GuardCount, 6);
		if (TestNotNull(TEXT("Loot layout"), D->LootLayout.Get()))
		{
			TArray<int32> First, Second;
			TestTrue(TEXT("Loot recipe selection"), D->LootLayout->Select(123, First, Error));
			D->LootLayout->Select(123, Second, Error);
			TestTrue(TEXT("Selection deterministic"), First == Second);
			TestEqual(TEXT("24 containers"), First.Num(), 24);
			TestEqual(TEXT("44 total authored sites"), D->LootLayout->Points.Num(), 44);
		}
	}
	return true;
}
#endif
