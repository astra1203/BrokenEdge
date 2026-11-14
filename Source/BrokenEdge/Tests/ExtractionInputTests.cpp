#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Extraction/Combat/ExtractionAbilities.h"
#include "BrokenEdge/Extraction/Core/ExtractionPlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionInputCancellation,
    "BrokenEdge.Extraction.Input.CancelHeldActions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionInputCancellation::RunTest(const FString&)
{
    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
                                      ERHIFeatureLevel::Num, &Init);
    if (!TestNotNull(TEXT("World"), World)) return false;
    auto* PC = World->SpawnActor<AExtractionPlayerController>();
    auto* C = World->SpawnActor<ABrokenEdgeCharacter>();
    if (!PC || !C) { World->DestroyWorld(false); return false; }
    PC->SetAsLocalPlayerController();
    PC->Possess(C);
    auto Hold = [&]
    {
        PC->bAimingGrenade = true;
        PC->bAimingGrenadeLow = true;
        PC->HealDownAt = World->GetTimeSeconds();
        PC->bHealHoldUsed = false;
        C->GetCombat()->FireButtonPressed(true);
    };
    auto CheckStopped = [&]
    {
        TestFalse(TEXT("Grenade aim cancelled without throwing"), PC->bAimingGrenade);
        TestFalse(TEXT("Low throw state cleared"), PC->bAimingGrenadeLow);
        TestEqual(TEXT("Healing gesture cancelled"), PC->HealDownAt, -1.0);
        TestFalse(TEXT("Held fire cleared"), C->GetCombat()->IsFireInputHeld());
        PC->HealReleased();
        TestFalse(TEXT("Late release after cancellation cannot draw stim"), C->GetExtractionAbilities()->bStimDrawn);
        TestEqual(TEXT("Cancellation never starts grenade cooldown"), PC->NextGrenadeAt, 0.0);
    };
    Hold();
    PC->ToggleExtractionMenu();
    CheckStopped();
    PC->ToggleExtractionMenu();
    Hold();
    PC->RequestOpenInventory(nullptr, true);
    CheckStopped();
    Hold();
    PC->HandleApplicationActivation(false);
    CheckStopped();
    Hold();
    C->bDisableGameplay = true;
    PC->StopHeldActions();
    CheckStopped();
    Hold();
    PC->UnPossess();
    CheckStopped();
    PC->StopHeldActions(); // No pawn and repeated cancellation are safe.
    World->DestroyWorld(false);
    return true;
}
#endif
