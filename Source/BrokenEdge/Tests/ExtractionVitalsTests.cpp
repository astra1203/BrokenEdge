#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Extraction/ExtractionState.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionVitalsTest, "BrokenEdge.Extraction.Vitals.AuthoritativeChanges",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FExtractionVitalsTest::RunTest(const FString&)
{
    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* W = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
    if (!W) return false;
    auto* C = W->SpawnActor<ABrokenEdgeCharacter>();
    int32 Events = 0;
    C->OnVitalsChanged.AddLambda([&](const FExtractionVitalsChange&) { ++Events; });
    auto Change = C->ApplyVitalsChange({-40, 30, EExtractionVitalsReason::Restore});
    TestEqual(TEXT("Combined health and shield produces one notification"), Events, 1);
    TestEqual(TEXT("Actual health delta"), Change.Health, -40.f);
    Change = C->ApplyResolvedDamage(45, nullptr, nullptr);
    TestEqual(TEXT("Legacy shield absorbed first"), Change.Shield, -30.f);
    TestEqual(TEXT("Remaining damage reaches health"), Change.Health, -15.f);
    TestEqual(TEXT("Damage notification once"), Events, 2);
    Change = C->ApplyVitalsChange({1000, 1000});
    TestEqual(TEXT("Healing clamps to maximum"), C->GetHealth(), C->GetMaxHealth());
    TestEqual(TEXT("Shield clamps to maximum"), C->GetShield(), C->GetMaxShield());
    TestEqual(TEXT("Actual heal excludes overflow"), Change.Health, 55.f);
    C->ApplyVitalsChange({10, 10});
    TestEqual(TEXT("No-op does not notify"), Events, 3);
    C->ApplyVitalsChange({std::numeric_limits<float>::quiet_NaN(), 1});
    TestEqual(TEXT("Invalid change is rejected atomically"), Events, 3);
    W->SetGameState(W->SpawnActor<AExtractionGameState>());
    Change = C->ApplyResolvedDamage(10, nullptr, nullptr);
    TestEqual(TEXT("Extraction damage bypasses legacy shield"), Change.Health, -10.f);
    TestEqual(TEXT("Extraction leaves legacy shield unchanged"), Change.Shield, 0.f);
    C->ApplyVitalsChange({-5, 0, EExtractionVitalsReason::Bleeding});
    TestEqual(TEXT("Bleeding changes health through same entry"), C->GetHealth(), 85.f);
    C->ApplyVitalsChange({4}); C->ApplyVitalsChange({6});
    TestEqual(TEXT("Independent healing sources compose"), C->GetHealth(), 95.f);
    C->OnVitalsChanged.Clear();
    W->DestroyWorld(false);
    return true;
}
#endif
