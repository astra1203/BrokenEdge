#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "GameFramework/PlayerController.h"
#include "BrokenEdge/Extraction/ExtractionGameMode.h"
#include "BrokenEdge/Extraction/ExtractionState.h"

namespace
{
	class FExtractionPIETeardown : public IAutomationLatentCommand
	{
	public:
		explicit FExtractionPIETeardown(FAutomationTestBase* InTest) : Test(InTest)
		{
		}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Stage == EStage::Start)
			{
				auto* Settings = NewObject<ULevelEditorPlaySettings>();
				Settings->bLaunchSeparateServer = false;
				Settings->SetPlayNumberOfClients(2);
				Settings->SetRunUnderOneProcess(true);
				Settings->SetPlayNetMode(Cycle < 2 ? PIE_ListenServer : PIE_Client);
				Settings->NewWindowWidth = 640;
				Settings->NewWindowHeight = 360;
				FRequestPlaySessionParams Params;
				Params.EditorPlaySettings = Settings;
				Params.bAllowOnlineSubsystem = false;
				GEditor->RequestPlaySession(Params);
				Stage = EStage::Running;
				Deadline = Now + 120;
				return false;
			}

			int32 Worlds = 0;
			UWorld* Server = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
				if (Context.WorldType == EWorldType::PIE)
				{
					++Worlds;
					if (Context.World() && Context.World()->GetAuthGameMode()) Server = Context.World();
				}

			if (Now > Deadline)
			{
				Test->AddError(FString::Printf(TEXT("PIE teardown cycle %d timed out in stage %d with %d worlds"),
				                               Cycle + 1, static_cast<int32>(Stage), Worlds));
				GEditor->RequestEndPlayMap();
				return true;
			}

			if (Stage == EStage::Running)
			{
				auto* Mode = Server ? Server->GetAuthGameMode<AExtractionGameMode>() : nullptr;
				auto* State = Server ? Server->GetGameState<AExtractionGameState>() : nullptr;
				if (Worlds != (Cycle < 2 ? 2 : 3) || !Mode || !State || State->PlayerArray.Num() != 2 ||
					State->Phase != EExtractionPhase::Ended)
					return false;
				Test->TestEqual(TEXT("Requested server topology"), Server->GetNetMode(),
				                Cycle < 2 ? NM_ListenServer : NM_DedicatedServer);
				Test->TestEqual(TEXT("Direct map requires lobby entry"), State->EndReason,
				                FString(TEXT("请从大厅准备后进入对局")));
				Test->TestNotNull(TEXT("Persistence exists before shutdown"), Mode->Store());
				int32 Players = 0;
				for (auto It = Server->GetPlayerControllerIterator(); It; ++It)
				{
					auto* Player = It->Get();
					auto* PS = Player->GetPlayerState<AExtractionPlayerState>();
					if (!Test->TestNotNull(TEXT("Extraction player"), PS)) continue;
					++Players;
					Test->TestFalse(TEXT("Direct map has no raid ticket"), PS->ExtractionRaid.IsValid());
					Test->TestFalse(TEXT("Invalid ticket cannot report settlement success"),
					                Mode->SettlePlayer(Player, EExtractionOutcome::Extracted));
					Test->TestTrue(
						TEXT("Rejected settlement leaves life unchanged"), PS->Life == EExtractionLife::Alive);
				}
				Test->TestEqual(TEXT("Two controllers reached shutdown boundary"), Players, 2);
				UE_LOG(LogTemp, Display, TEXT("EXTRACTION_PIE_TEARDOWN_READY Cycle=%d Worlds=%d Players=%d"),
				       Cycle + 1, Worlds, Players);
				GEditor->RequestEndPlayMap();
				Stage = EStage::Stopping;
				Deadline = Now + 60;
				return false;
			}

			if (Stage == EStage::Stopping)
			{
				if (Worlds || GEditor->PlayWorld) return false;
				Stage = EStage::Stopped;
				StoppedAt = Now;
				return false;
			}
			// Wait for subsequent editor ticks after cleanup before restarting or reporting success.
			if (Worlds || GEditor->PlayWorld)
			{
				Test->AddError(TEXT("PIE world survived cleanup"));
				GEditor->RequestEndPlayMap();
				return true;
			}
			if (Now - StoppedAt < 1) return false;
			UE_LOG(LogTemp, Display, TEXT("EXTRACTION_PIE_TEARDOWN_STOPPED Cycle=%d Worlds=0"), Cycle + 1);
			if (++Cycle == 4) return true;
			Stage = EStage::Start;
			return false;
		}

	private:
		enum class EStage { Start, Running, Stopping, Stopped };

		FAutomationTestBase* Test;
		EStage Stage = EStage::Start;
		int32 Cycle = 0;
		double Deadline = 0;
		double StoppedAt = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionPIETeardownTest, "BrokenEdge.Extraction.PIE.Teardown",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionPIETeardownTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->PlayWorld)
	{
		AddError(TEXT("Run in an idle isolated editor"));
		return false;
	}
	if (!FEditorFileUtils::LoadMap(TEXT("/Game/Maps/SpaceCenter/L_SpaceCenterExtraction"), false, false))
	{
		AddError(TEXT("Could not load extraction map"));
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FExtractionPIETeardown(this));
	return true;
}
#endif
