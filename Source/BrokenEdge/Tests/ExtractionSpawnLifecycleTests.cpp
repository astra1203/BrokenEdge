#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Extraction/Core/ExtractionGameMode.h"
#include "BrokenEdge/Extraction/Core/ExtractionPlayerController.h"
#include "BrokenEdge/Extraction/Core/ExtractionState.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerStart.h"

// Exercise the production waiting-player loop in a real temporary World. The cached start and
// lightweight pawn isolate controller replacement from floor probing, assets and persistent stores.
struct FExtractionSpawnLifecycleFixture
{
	UWorld* World = nullptr;
	AExtractionGameMode* Mode = nullptr;
	AExtractionGameState* State = nullptr;
	TArray<APlayerController*> Controllers;

	FExtractionSpawnLifecycleFixture()
	{
		const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		                                                .CreateNavigation(false).CreateAISystem(false).
		                                                ShouldSimulatePhysics(false);
		World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		                            ERHIFeatureLevel::Num, &Init);
		if (!World) return;
		World->SetGameInstance(NewObject<UGameInstance>(World));
		Mode = World->SpawnActor<AExtractionGameMode>();
		State = World->SpawnActor<AExtractionGameState>();
		if (!Mode || !State) return;
		World->SetGameState(State);
		Mode->GameState = State;
		Mode->DefaultPawnClass = ADefaultPawn::StaticClass();
		State->Phase = EExtractionPhase::Waiting;
	}

	~FExtractionSpawnLifecycleFixture()
	{
		for (auto* Controller : Controllers)
			if (IsValid(Controller)) Controller->UnPossess();
		if (World) World->DestroyWorld(false);
	}

	bool IsReady() const { return World && Mode && State; }

	APlayerController* AddController(bool bExtractionClass, bool bCurrentConnection, bool bExtractionState)
	{
		if (!IsReady()) return nullptr;
		auto* PC = bExtractionClass
			           ? static_cast<APlayerController*>(World->SpawnActor<AExtractionPlayerController>())
			           : static_cast<APlayerController*>(World->SpawnActor<ABrokenEdgePlayerController>());
		if (!PC) return nullptr;
		// This unstarted fixture does not run PostInitializeComponents, so register explicitly.
		World->AddController(PC);
		Controllers.Add(PC);
		if (bExtractionState)
		{
			auto* PS = World->SpawnActor<AExtractionPlayerState>();
			PC->PlayerState = PS;
			if (PS) PS->SetOwner(PC);
		}
		if (bCurrentConnection)
		{
			auto* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
			PC->Player = LocalPlayer;
			LocalPlayer->PlayerController = PC;
		}
		return PC;
	}

	void GiveCachedStart(APlayerController* PC)
	{
		auto* Start = World->SpawnActor<APlayerStart>(FVector(1000, 0, 500), FRotator::ZeroRotator);
		Mode->PlayerStarts.Add(PC, Start);
	}

	int32 RegisteredControllers() const
	{
		int32 Count = 0;
		for (auto It = World->GetPlayerControllerIterator(); It; ++It) ++Count;
		return Count;
	}

	int32 RetryAndCount() { return Mode->StartWaitingPlayers(); }
	int32 Failures() const { return Mode->SpawnAttemptWarnings.Num(); }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionRetiredControllerSpawnTest,
                                 "BrokenEdge.Extraction.SpawnLifecycle.RetiredControllersDoNotRestart",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionRetiredControllerSpawnTest::RunTest(const FString&)
{
	FExtractionSpawnLifecycleFixture F;
	if (!TestTrue(TEXT("Temporary mode and waiting state exist"), F.IsReady())) return false;
	auto* OldLobby = F.AddController(false, false, false);
	auto* OldExtraction = F.AddController(true, false, false);
	auto* Current = F.AddController(true, true, true);
	if (!TestNotNull(TEXT("Old lobby controller remains registered"), OldLobby)
		|| !TestNotNull(TEXT("Retired extraction controller remains registered"), OldExtraction)
		|| !TestNotNull(TEXT("Converted controller exists"), Current))
		return false;
	TestEqual(TEXT("World iterator includes both retired controllers and the replacement"), F.RegisteredControllers(),
	          3);
	TestTrue(TEXT("The engine reports a detached old controller as loaded"), OldLobby->HasClientLoadedCurrentWorld());
	TestNull(TEXT("Old controller has the cleared PlayerState from seamless replacement"), OldLobby->PlayerState.Get());
	F.GiveCachedStart(Current);
	TestTrue(TEXT("A valid current waiting player may restart before InProgress"), F.Mode->PlayerCanRestart(Current));
	F.Mode->HandleStartingNewPlayer(OldLobby);
	F.Mode->HandleStartingNewPlayer(OldExtraction);
	TestEqual(TEXT("Actual waiting loop spawns and counts only the converted player"), F.RetryAndCount(), 1);
	TestNotNull(TEXT("The converted player went through RestartPlayer and received a pawn"), Current->GetPawn().Get());
	TestNull(TEXT("No pawn was spawned for old lobby controller"), OldLobby->GetPawn().Get());
	TestNull(TEXT("No pawn was spawned for retired extraction controller"), OldExtraction->GetPawn().Get());
	TestEqual(TEXT("Retired controllers produced no spawn failure"), F.Failures(), 0);
	auto* Pawn = Current->GetPawn().Get();
	TestEqual(TEXT("Repeated waiting updates still count exactly one player"), F.RetryAndCount(), 1);
	TestTrue(TEXT("Repeated waiting does not replace the existing pawn"), Current->GetPawn() == Pawn);
	TestFalse(TEXT("An already spawned player cannot restart again"), F.Mode->PlayerCanRestart(Current));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionRetiredControllerReadyTest,
                                 "BrokenEdge.Extraction.SpawnLifecycle.RetiredPawnDoesNotCountReady",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionRetiredControllerReadyTest::RunTest(const FString&)
{
	FExtractionSpawnLifecycleFixture F;
	if (!TestTrue(TEXT("Temporary mode and waiting state exist"), F.IsReady())) return false;
	auto* Retired = F.AddController(true, false, true);
	auto* Current = F.AddController(true, true, true);
	if (!TestNotNull(TEXT("Retired controller exists"), Retired)
		|| !TestNotNull(TEXT("Current controller exists"), Current))
		return false;
	auto* OldPawn = F.World->SpawnActor<ADefaultPawn>();
	Retired->Possess(OldPawn);
	TestTrue(TEXT("Retired actor can still carry a pawn during teardown"), Retired->GetPawn() == OldPawn && OldPawn);
	F.GiveCachedStart(Current);
	TestEqual(TEXT("Loaded leftover pawn cannot inflate the production ready count"), F.RetryAndCount(), 1);
	TestFalse(TEXT("Retired controller cannot request another spawn"), F.Mode->PlayerCanRestart(Retired));
	TestEqual(TEXT("Filtering retired ownership is not reported as a spawn failure"), F.Failures(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionCurrentControllerStateTest,
                                 "BrokenEdge.Extraction.SpawnLifecycle.CurrentMissingStateIsAnError",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionCurrentControllerStateTest::RunTest(const FString&)
{
	FExtractionSpawnLifecycleFixture F;
	if (!TestTrue(TEXT("Temporary mode and waiting state exist"), F.IsReady())) return false;
	auto* Current = F.AddController(true, true, false);
	if (!TestNotNull(TEXT("Current connected extraction controller exists"), Current)) return false;
	AddExpectedError(TEXT("EXTRACTION_SPAWN_FAIL no_player_state"), EAutomationExpectedErrorFlags::Contains, 1);
	F.Mode->HandleStartingNewPlayer(Current);
	TestEqual(TEXT("Real initialization error prevents readiness"), F.RetryAndCount(), 0);
	TestNull(TEXT("A current controller without ExtractionPlayerState receives no pawn"), Current->GetPawn().Get());
	TestEqual(TEXT("Real missing state remains an Error, deduplicated per controller"), F.Failures(), 1);
	return true;
}
#endif
