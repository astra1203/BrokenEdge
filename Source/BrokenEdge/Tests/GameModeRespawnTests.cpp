#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/GameMode/BrokenEdgeGameMode.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrokenEdgeRespawnEmptyStarts, "BrokenEdge.GameMode.Respawn.EmptyStarts",
                                EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBrokenEdgeRespawnEmptyStarts::RunTest(const FString&)
{
    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
                                      ERHIFeatureLevel::Num, &Init);
    if (!TestNotNull(TEXT("Temporary world"), World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Mode = World->SpawnActor<ABrokenEdgeGameMode>();
    auto* PC = World->SpawnActor<APlayerController>();
    if (!TestNotNull(TEXT("Game mode"), Mode) || !TestNotNull(TEXT("Controller"), PC))
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }
    Mode->DefaultPawnClass = ADefaultPawn::StaticClass();
    auto* Character = World->SpawnActor<ACharacter>();
    Mode->RequestRespawn(Character, nullptr);
    TestTrue(TEXT("Invalid controller leaves defeated character untouched"), IsValid(Character));
    if (Character) Character->Destroy();

    AddExpectedError(TEXT("RESPAWN_NO_PLAYERSTART"), EAutomationExpectedErrorFlags::Contains, 1);
    Mode->RequestRespawn(nullptr, PC);
    TestNull(TEXT("No start does not fabricate a pawn at the origin"), PC->GetPawn());
    auto* First = World->SpawnActor<APlayerStart>(FVector(1000, 0, 500), FRotator::ZeroRotator);
    Mode->RequestRespawn(nullptr, PC);
    TestTrue(TEXT("Single start selected"), PC->GetPawn() && First &&
        PC->GetPawn()->GetActorLocation().Equals(First->GetActorLocation(), 1.f));
    if (auto* Pawn = PC->GetPawn().Get()) { PC->UnPossess(); Pawn->Destroy(); }
    auto* Second = World->SpawnActor<APlayerStart>(FVector(2000, 0, 500), FRotator::ZeroRotator);
    Mode->RequestRespawn(nullptr, PC);
    TestTrue(TEXT("Multiple starts select a valid start"), PC->GetPawn() && First && Second &&
        (PC->GetPawn()->GetActorLocation().Equals(First->GetActorLocation(), 1.f) ||
         PC->GetPawn()->GetActorLocation().Equals(Second->GetActorLocation(), 1.f)));
    PC->UnPossess();
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
