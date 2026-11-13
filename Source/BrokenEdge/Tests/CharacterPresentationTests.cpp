#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Character/BrokenEdgeCharacterPresentation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterPresentationTest, "BrokenEdge.Extraction.Presentation.CharacterContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCharacterPresentationTest::RunTest(const FString&)
{
    for (const TCHAR* Name : {TEXT("EnemyCharacter"), TEXT("BrokenEdgeCharacter"), TEXT("BrokenEdgeCharacter1"),
                              TEXT("StingerCharacter"), TEXT("StingerCharacter_Repaired")})
    {
        const FString Path = FString::Printf(TEXT("/Game/Characters/Presentation/DA_%s_Presentation"), Name);
        auto* Asset = LoadObject<UBrokenEdgeCharacterPresentation>(nullptr, *Path);
        if (!TestNotNull(*Path, Asset)) return false;
        for (const auto& Error : Asset->ValidatePresentation()) AddError(Path + TEXT(": ") + Error);
    }
    auto* Original = LoadObject<UBrokenEdgeCharacterPresentation>(nullptr,
        TEXT("/Game/Characters/Presentation/DA_StingerCharacter_Presentation"));
    auto* Custom = DuplicateObject<UBrokenEdgeCharacterPresentation>(Original, GetTransientPackage());
    Custom->HeadBones = {TEXT("AlternateHead")};
    Custom->LegBones = {TEXT("AlternateLeg")}; Custom->LegPrefixes.Reset();
    TestEqual(TEXT("Alternate head configuration controls armor"), Custom->ArmorSlotForBone(TEXT("AlternateHead")), 2);
    TestEqual(TEXT("Old head mapping does not leak into alternate model"), Custom->ArmorSlotForBone(TEXT("head")), INDEX_NONE);
    TestTrue(TEXT("Alternate leg configuration controls trauma"), Custom->IsLeg(TEXT("AlternateLeg")));
    TestFalse(TEXT("Old rig prefix can be removed"), Custom->IsLeg(TEXT("thigh_l")));
    Custom->WeaponHandSocket = TEXT("MissingContractSocket");
    TestTrue(TEXT("Validator identifies missing attachment by name"), Custom->ValidatePresentation().ContainsByPredicate(
        [](const FString& Error) { return Error.Contains(TEXT("MissingContractSocket")); }));

    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* W = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
    if (!TestNotNull(TEXT("Fixture world"), W)) return false;
    auto* C = W->SpawnActor<ABrokenEdgeCharacter>();
    C->CharacterPresentation = Custom;
    C->PostInitializeComponents();
    TestTrue(TEXT("Runtime retains the selected model contract"), &C->GetPresentation() == Custom);
    TestEqual(TEXT("Runtime applies configured world mesh"), C->GetMesh()->GetSkeletalMeshAsset(), Custom->WorldMesh.Get());
    TestEqual(TEXT("Runtime damage reads the selected model contract"), C->GetPresentation().ArmorSlotForBone(TEXT("AlternateHead")), 2);
    W->DestroyWorld(false);
    return true;
}
#endif
