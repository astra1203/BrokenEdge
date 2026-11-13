#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ExtractionInventoryTestAccess.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Extraction/ExtractionExit.h"
#include "BrokenEdge/Extraction/ExtractionGameMode.h"
#include "BrokenEdge/Extraction/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/ExtractionPlayerController.h"
#include "BrokenEdge/Extraction/ExtractionState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

// Drive the production GameMode against real actors without starting a match or opening a player store.
// The friend only exposes the existing private update and exit list to this temporary-world fixture.
struct FExtractionExitFeedbackFixture
{
	struct FPlayer
	{
		AExtractionPlayerController* Controller = nullptr;
		AExtractionPlayerState* State = nullptr;
		ABrokenEdgeCharacter* Pawn = nullptr;
		bool IsReady() const { return Controller && State && State->Inventory && Pawn; }

		bool IsRegistered() const
		{
			if (!IsReady() || Controller->GetPawn() != Pawn
				|| Controller->GetPlayerState<AExtractionPlayerState>() != State)
				return false;
			for (auto It = Controller->GetWorld()->GetPlayerControllerIterator(); It; ++It)
				if (It->Get() == Controller) return true;
			return false;
		}
	};

	UWorld* World = nullptr;
	AExtractionGameMode* Mode = nullptr;
	TArray<FPlayer> Players;

	FExtractionExitFeedbackFixture()
	{
		const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		                                                .CreateNavigation(false).CreateAISystem(false).
		                                                ShouldSimulatePhysics(false);
		World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		                            ERHIFeatureLevel::Num, &Init);
		if (!World) return;
		World->SetGameInstance(NewObject<UGameInstance>(World));
		Mode = World->SpawnActor<AExtractionGameMode>();
	}

	~FExtractionExitFeedbackFixture()
	{
		for (const auto& Player : Players)
			if (IsValid(Player.Controller)) Player.Controller->UnPossess();
		if (World) World->DestroyWorld(false);
	}

	FPlayer AddPlayer(FVector Location = FVector::ZeroVector)
	{
		FPlayer Player;
		if (!World || !Mode) return Player;
		Player.Controller = World->SpawnActor<AExtractionPlayerController>();
		Player.State = World->SpawnActor<AExtractionPlayerState>();
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Player.Pawn = World->SpawnActor<ABrokenEdgeCharacter>(Location, FRotator::ZeroRotator, Spawn);
		if (Player.IsReady())
		{
			// This world deliberately never initializes gameplay actors. SpawnActor therefore skips
			// PostInitializeComponents, where AController normally registers with the world's iterator.
			World->AddController(Player.Controller);
			Player.Controller->PlayerState = Player.State;
			Player.Controller->Possess(Player.Pawn);
			Player.Pawn->SetPlayerState(Player.State);
			FExtractionInventoryTestAccess::Get(Player.State->Inventory).Items.Reset();
		}
		Players.Add(Player);
		return Player;
	}

	AExtractionExit* AddExit(FName Id, FVector Location = FVector::ZeroVector)
	{
		if (!World || !Mode) return nullptr;
		auto* Exit = World->SpawnActor<AExtractionExit>();
		if (!Exit) return nullptr;
		FExtractionExitDefinition Definition;
		Definition.Id = Id;
		Definition.Label = FText::FromName(Id);
		Definition.Transform.SetLocation(Location);
		Definition.HalfExtent = FVector(200);
		Definition.BoundsOffset = FVector::ZeroVector;
		Definition.HoldSeconds = 10;
		Exit->Configure(Definition);
		Exit->StartRound(0);
		Mode->Exits.Add(Exit);
		return Exit;
	}

	void Update(double Now)
	{
		World->TimeSeconds = Now;
		Mode->UpdateExtraction(Now);
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionExitAvailabilityFeedback,
                                 "BrokenEdge.Extraction.ExitFeedback.AvailabilityTransitions",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionExitAvailabilityFeedback::RunTest(const FString&)
{
	FExtractionExitFeedbackFixture F;
	const auto Player = F.AddPlayer();
	auto* Exit = F.AddExit(TEXT("TimedExit"));
	if (!TestTrue(TEXT("Temporary player is registered with its pawn and state"), Player.IsRegistered())
		|| !TestNotNull(TEXT("Temporary exit exists"), Exit))
		return false;
	Exit->Definition.OpensAfterSeconds = 10;
	Exit->Definition.ClosesAfterSeconds = 30;
	Exit->Definition.Capacity = 1;

	F.Update(0);
	TestEqual(TEXT("Closed area is still the selected exit"), Player.State->ExtractExitId, Exit->Definition.Id);
	TestEqual(TEXT("Opening countdown uses the exit's actual reason"), Player.State->ExtractBlockedReason,
	          Exit->UnavailableReason(0));
	TestEqual(TEXT("Unavailable exit cannot start hold time"), Player.State->ExtractAt, 0.0);
	F.Update(10);
	TestTrue(TEXT("Opening clears the blocked reason"), Player.State->ExtractBlockedReason.IsEmpty());
	TestEqual(TEXT("Opening starts a full hold period"), Player.State->ExtractAt, 20.0);
	F.Update(11);
	TestEqual(TEXT("Continuous presence preserves the deadline"), Player.State->ExtractAt, 20.0);

	Exit->UsedCapacity = 1;
	F.Update(12);
	TestEqual(TEXT("Capacity exhaustion cancels a running hold"), Player.State->ExtractAt, 0.0);
	TestEqual(TEXT("Capacity reason is published centrally"), Player.State->ExtractBlockedReason,
	          Exit->UnavailableReason(12));
	Exit->UsedCapacity = 0;
	F.Update(13);
	TestEqual(TEXT("Restored availability restarts the full hold"), Player.State->ExtractAt, 23.0);
	F.Update(30);
	TestEqual(TEXT("Closing boundary cancels rather than settling an expired hold"), Player.State->ExtractAt, 0.0);
	TestEqual(TEXT("Closing reason is retained in the area"), Player.State->ExtractBlockedReason,
	          Exit->UnavailableReason(30));
	TestEqual(TEXT("No unavailable transition consumes a slot"), Exit->UsedCapacity, 0);
	TestTrue(TEXT("No unavailable transition settles the player"), Player.State->Life == EExtractionLife::Alive);

	Player.Pawn->SetActorLocation(FVector(1000, 0, 0));
	F.Update(31);
	TestTrue(TEXT("Leaving clears exit identity"), Player.State->ExtractExitId.IsNone());
	TestTrue(TEXT("Leaving clears central reason"), Player.State->ExtractBlockedReason.IsEmpty());
	TestEqual(TEXT("Leaving clears the deadline"), Player.State->ExtractAt, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionExitPersonalFeedback,
                                 "BrokenEdge.Extraction.ExitFeedback.PersonalConditionsAndIsolation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionExitPersonalFeedback::RunTest(const FString&)
{
	FExtractionExitFeedbackFixture F;
	const auto A = F.AddPlayer();
	const auto B = F.AddPlayer(FVector(80, 0, 0));
	auto* Exit = F.AddExit(TEXT("EmptyPackExit"));
	if (!TestTrue(
			TEXT("Both temporary players are registered with their pawns and states"),
			A.IsRegistered() && B.IsRegistered())
		|| !TestNotNull(TEXT("Temporary exit exists"), Exit))
		return false;
	Exit->Definition.bRequireEmptyPack = true;
	Exit->Definition.OpensAfterSeconds = 10;
	FExtractionItem Item;
	Item.Id = FGuid::NewGuid();
	// 丢包撤只看身上是否穿着背包。
	Item.Definition = TEXT("Bag_ALS");
	Item.Quantity = 1;
	Item.Slot = ExtractionGear::BagSlot;
	FExtractionInventoryTestAccess::Get(A.State->Inventory).Items.Add(Item);

	F.Update(0);
	TestEqual(TEXT("Availability takes precedence over personal inventory"), A.State->ExtractBlockedReason,
	          Exit->UnavailableReason(0));
	F.Update(10);
	TestTrue(TEXT("Occupied pack blocks only its owner"), !A.State->ExtractBlockedReason.IsEmpty());
	TestEqual(TEXT("Blocked owner has no deadline"), A.State->ExtractAt, 0.0);
	TestTrue(TEXT("Second player's reason remains empty"), B.State->ExtractBlockedReason.IsEmpty());
	TestEqual(TEXT("Second player starts normally"), B.State->ExtractAt, 20.0);

	FExtractionInventoryTestAccess::Get(A.State->Inventory).Items.Reset();
	F.Update(11);
	TestTrue(TEXT("Removing inventory clears the personal reason"), A.State->ExtractBlockedReason.IsEmpty());
	TestEqual(TEXT("Clearing the condition starts a fresh deadline"), A.State->ExtractAt, 21.0);
	TestEqual(TEXT("Other player's ongoing deadline is unchanged"), B.State->ExtractAt, 20.0);
	FExtractionInventoryTestAccess::Get(A.State->Inventory).Items.Add(Item);
	F.Update(12);
	TestEqual(TEXT("Adding pack inventory cancels the hold"), A.State->ExtractAt, 0.0);
	TestEqual(TEXT("One player's cancellation does not reset another"), B.State->ExtractAt, 20.0);
	A.Pawn->SetActorLocation(FVector(1000, 0, 0));
	F.Update(13);
	TestTrue(TEXT("Leaving clears personal block and identity"), A.State->ExtractBlockedReason.IsEmpty()
	         && A.State->ExtractExitId.IsNone());
	TestEqual(TEXT("Leaving does not change another player's countdown"), B.State->ExtractAt, 20.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionExitOverlapFeedback,
                                 "BrokenEdge.Extraction.ExitFeedback.OverlappingAreas",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionExitOverlapFeedback::RunTest(const FString&)
{
	FExtractionExitFeedbackFixture F;
	const auto Player = F.AddPlayer(FVector(50, 0, 0));
	auto* First = F.AddExit(TEXT("FirstExit"));
	auto* Second = F.AddExit(TEXT("SecondExit"), FVector(100, 0, 0));
	if (!TestTrue(TEXT("Temporary player is registered with its pawn and state"), Player.IsRegistered())
		|| !TestNotNull(TEXT("First exit exists"), First)
		|| !TestNotNull(TEXT("Second exit exists"), Second))
		return false;
	F.Update(0);
	TestEqual(TEXT("Initial overlap follows map configuration order"), Player.State->ExtractExitId,
	          First->Definition.Id);
	Player.Pawn->SetActorLocation(FVector(250, 0, 0));
	F.Update(1);
	TestEqual(TEXT("Leaving first area selects second"), Player.State->ExtractExitId, Second->Definition.Id);
	TestEqual(TEXT("Switching areas restarts hold duration"), Player.State->ExtractAt, 11.0);
	Player.Pawn->SetActorLocation(FVector(50, 0, 0));
	F.Update(2);
	TestEqual(TEXT("Reentering overlap keeps current exit"), Player.State->ExtractExitId, Second->Definition.Id);
	TestEqual(TEXT("Overlap does not reset current hold"), Player.State->ExtractAt, 11.0);
	Second->Definition.Capacity = 1;
	Second->UsedCapacity = 1;
	F.Update(3);
	TestEqual(TEXT("Unavailable current exit is not silently replaced"), Player.State->ExtractExitId,
	          Second->Definition.Id);
	TestEqual(TEXT("Unavailable overlap publishes current exit reason"), Player.State->ExtractBlockedReason,
	          Second->UnavailableReason(3));
	TestEqual(TEXT("Unavailable overlap cannot retain hold progress"), Player.State->ExtractAt, 0.0);
	Player.Pawn->SetActorLocation(FVector(-150, 0, 0));
	F.Update(4);
	TestEqual(TEXT("Leaving blocked area permits the other exit"), Player.State->ExtractExitId, First->Definition.Id);
	TestTrue(TEXT("Switching to usable exit clears old reason"), Player.State->ExtractBlockedReason.IsEmpty());
	TestEqual(TEXT("New exit starts its own full hold"), Player.State->ExtractAt, 14.0);
	return true;
}
#endif
