#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "BrokenEdge/Component/BuffComponent.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Extraction/Core/ExtractionState.h"
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
    // A frame can cross the configured end time; the requested budget is still exact.
    C->PostInitializeComponents();
    auto* Buff = C->FindComponentByClass<UBuffComponent>();
    if (TestNotNull(TEXT("Character owns its initialized buff component"), Buff))
    {
        auto ResetBudgets = [&]
        {
            C->ApplyVitalsChange({30 - C->GetHealth(), 20 - C->GetShield(), EExtractionVitalsReason::Restore});
            Events = 0;
        };
        ResetBudgets();
        Buff->BuffHeal(10, 1); Buff->ReplenishShield(20, 1);
        Buff->TickComponent(.75f, LEVELTICK_All, nullptr);
        TestEqual(TEXT("Healing advances at configured rate"), C->GetHealth(), 37.5f);
        TestEqual(TEXT("Shield advances at configured rate"), C->GetShield(), 35.f);
        Buff->TickComponent(.75f, LEVELTICK_All, nullptr);
        TestEqual(TEXT("Last healing frame cannot exceed remaining budget"), C->GetHealth(), 40.f);
        TestEqual(TEXT("Last shield frame cannot exceed remaining budget"), C->GetShield(), 40.f);
        const int32 CompletedEvents = Events;
        Buff->TickComponent(10, LEVELTICK_All, nullptr);
        TestEqual(TEXT("Finished buffs do not notify again"), Events, CompletedEvents);

        const float NaN = std::numeric_limits<float>::quiet_NaN();
        const float Infinity = std::numeric_limits<float>::infinity();
        const float Smallest = std::numeric_limits<float>::min(), Largest = std::numeric_limits<float>::max();
        const FVector2D InvalidRequests[] = {{10, 0}, {10, -1}, {-10, 1}, {0, 1}, {NaN, 1}, {10, NaN}, {Infinity, 1}, {10, Infinity},
            {Largest, Smallest}, {Smallest, Largest}};
        for (int32 I = 0; I < UE_ARRAY_COUNT(InvalidRequests); ++I)
        {
            ResetBudgets();
            Buff->BuffHeal(10, 1); Buff->ReplenishShield(20, 1);
            Buff->BuffHeal(InvalidRequests[I].X, InvalidRequests[I].Y);
            Buff->ReplenishShield(InvalidRequests[I].X, InvalidRequests[I].Y);
            Buff->TickComponent(1, LEVELTICK_All, nullptr);
            TestEqual(*FString::Printf(TEXT("Invalid healing request %d preserves active budget"), I), C->GetHealth(), 40.f);
            TestEqual(*FString::Printf(TEXT("Invalid shield request %d preserves active budget"), I), C->GetShield(), 40.f);
        }
        ResetBudgets();
        Buff->BuffHeal(10, 1); Buff->ReplenishShield(20, 1);
        Buff->TickComponent(-1, LEVELTICK_All, nullptr);
        Buff->TickComponent(NaN, LEVELTICK_All, nullptr);
        Buff->TickComponent(Infinity, LEVELTICK_All, nullptr);
        Buff->TickComponent(0, LEVELTICK_All, nullptr);
        TestEqual(TEXT("Invalid frame intervals cannot change vitals"), Events, 0);
        Buff->TickComponent(1, LEVELTICK_All, nullptr);
        TestEqual(TEXT("Invalid frame interval preserves healing budget"), C->GetHealth(), 40.f);
        TestEqual(TEXT("Invalid frame interval preserves shield budget"), C->GetShield(), 40.f);
    }
    C->OnVitalsChanged.Clear();
    W->DestroyWorld(false);
    return true;
}
#endif
