#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Button.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "BrokenEdge/GameState/LobbyGameState.h"
#include "BrokenEdge/UI/LobbyWidget.h"
#include "BrokenEdge/Extraction/ExtractionAccountComponent.h"
#include "BrokenEdge/Extraction/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/ExtractionState.h"
#include "MultiplayerSessionsSubsystem.h"

namespace
{
	class FExtractionPIELobbyFlow : public IAutomationLatentCommand
	{
	public:
		FExtractionPIELobbyFlow(FAutomationTestBase* InTest, FString InMode)
			: Test(InTest), Mode(MoveTemp(InMode))
		{
			Seamless = IConsoleManager::Get().FindConsoleVariable(TEXT("net.AllowPIESeamlessTravel"));
			InitialSeamless = Seamless->GetInt();
			FParse::Value(FCommandLine::Get(), TEXT("ExtractionPIEPlayers="), Players);
			Deadline = FPlatformTime::Seconds() + 120;
		}

		virtual ~FExtractionPIELobbyFlow() override { Seamless->SetWithCurrentPriority(InitialSeamless); }

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			UWorld* Server = nullptr;
			TArray<UWorld*> Clients;
			int32 Worlds = 0;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
				if (Context.WorldType == EWorldType::PIE)
				{
					++Worlds;
					if (auto* World = Context.World())
					{
						if (World->GetNetMode() == NM_DedicatedServer) Server = World;
						else if (World->GetNetMode() == NM_Client) Clients.Add(World);
					}
				}
			if (Now > Deadline)
			{
				Test->AddError(FString::Printf(TEXT("PIE lobby timeout Mode=%s Stage=%d Round=%d Worlds=%d"),
				                               *Mode, Stage, Round, Worlds));
				GEditor->RequestEndPlayMap();
				return true;
			}
			if (Stage == 6)
			{
				if (Worlds || GEditor->PlayWorld) return false;
				if (Since == 0) Since = Now;
				if (Now - Since < 1) return false;
				UE_LOG(LogTemp, Display, TEXT("EXTRACTION_PIE_LOBBY_STOPPED Mode=%s Rounds=%d Worlds=0"), *Mode, Round);
				return true;
			}
			if (!Server || Clients.Num() != Players || Server->IsInSeamlessTravel()) return false;
			for (auto* Client : Clients) if (Client->IsInSeamlessTravel() || !Client->GetFirstPlayerController()) return
				false;
			auto* Lobby = Server->GetGameState<ALobbyGameState>();
			auto* Store = Server->GetGameInstance()->GetSubsystem<UExtractionPersistence>();

			if (Stage == 0)
			{
				if (!Lobby || Lobby->PlayerArray.Num() != Players || Lobby->RoomMatchType != Mode) return false;
				for (auto* Client : Clients) if (!ReadyButton(Client)) return false;
				if (Mode != TEXT("Extraction"))
				{
					for (auto* Client : Clients) ReadyButton(Client)->OnClicked.Broadcast();
					Advance(3);
					return false;
				}
				for (auto* Client : Clients)
				{
					const auto* Account = Client->GetFirstPlayerController()->FindComponentByClass<
						UExtractionAccountComponent>();
					if (!Account || !Account->Bound || !Account->Profile.Profile.IsValid()) return false;
				}
				for (auto* Client : Clients)
				{
					auto* Account = Client->GetFirstPlayerController()->FindComponentByClass<
						UExtractionAccountComponent>();
					const FGuid Profile = Account->Profile.Profile;
					const auto* Item = Account->Profile.Stash.Items.FindByPredicate([&](const auto& I)
					{
						return Round == 0 ? I.Definition == TEXT("AK") : Weapons.FindRef(Profile) == I.Id;
					});
					if (!Item)
					{
						Test->AddError(TEXT("Expected rifle absent from returned stash"));
						return true;
					}
					Weapons.Add(Profile, Item->Id);
					Squads.FindOrAdd(Profile) = Client->GetFirstPlayerController()->GetPlayerState<ABrokenEdgePlayerState>()->ExtractionSquad;
					FExtractionStorageRequest Request;
					Request.Move.Item = Item->Id;
					Request.Move.Quantity = 1;
					Request.Move.Slot = 0;
					Account->RequestMove(Request);
				}
				Advance(1);
				return false;
			}
			if (Stage == 1)
			{
				for (auto* Client : Clients)
				{
					const auto* Account = Client->GetFirstPlayerController()->FindComponentByClass<
						UExtractionAccountComponent>();
					if (!Account || !Account->Profile.Loadout.InSlot(0) || !ReadyButton(Client)) return false;
				}
				if (Round == 0)
				{
					BeforeBlocked = Store->TestState().Sequence;
					Seamless->SetWithCurrentPriority(0);
				}
				for (auto* Client : Clients) ReadyButton(Client)->OnClicked.Broadcast();
				Advance(Round == 0 ? 2 : 3);
				Since = Now;
				return false;
			}
			if (Stage == 2)
			{
				if (Now - Since < 5) return false;
				if (!Test->TestNotNull(TEXT("Disabled seamless travel stays in lobby"), Lobby)) return true;
				Test->TestTrue(TEXT("Clear PIE configuration error"), Lobby->LobbyStatus.Contains(TEXT("PIE 未启用无缝切图")));
				Test->TestEqual(TEXT("Blocked start does not write store"), Store->TestState().Sequence, BeforeBlocked);
				Test->TestEqual(TEXT("Blocked start creates no raid"), Store->TestState().Raids.Num(), 0);
				for (const auto& Pair : Weapons)
					Test->TestNotNull(
						TEXT("Blocked start keeps equipped rifle"), Store->View(Pair.Key)->Loadout.InSlot(0));
				Seamless->SetWithCurrentPriority(InitialSeamless);
				UE_LOG(LogTemp, Display, TEXT("EXTRACTION_PIE_LOBBY_DISABLED_GUARD_OK"));
				Advance(3);
				return false;
			}
			if (Stage == 3)
			{
				if (Mode != TEXT("Extraction"))
				{
					if (!Server->GetPathName().Contains(Mode) || Lobby || !Server->GetGameState()
						|| Server->GetGameState()->PlayerArray.Num() != Players)
						return false;
					for (auto* Client : Clients) if (!Client->GetPathName().Contains(Mode)) return false;
					Test->TestTrue(TEXT("Legacy online session retained"),
					               Server->GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>()->
					                       HasSession());
					Round = 1;
					UE_LOG(LogTemp, Display, TEXT("EXTRACTION_PIE_LOBBY_LEGACY_OK Mode=%s"), *Mode);
					Stop();
					return false;
				}
				const auto* Game = Server->GetGameState<AExtractionGameState>();
				if (!Game || Game->Phase != EExtractionPhase::Active || Game->PlayerArray.Num() != Players) return false;
				for (auto* Client : Clients)
				{
					const auto* PS = Client->GetFirstPlayerController()->GetPlayerState<AExtractionPlayerState>();
					if (!PS || !PS->Inventory || !PS->Inventory->Contents.InSlot(0)) return false;
				}
				FGuid Current;
				for (APlayerState* State : Game->PlayerArray)
				{
					const auto* PS = Cast<AExtractionPlayerState>(State);
					if (!Test->TestNotNull(TEXT("Extraction PlayerState after seamless travel"), PS)) return true;
					Test->TestTrue(TEXT("Authenticated profile retained"), Weapons.Contains(PS->ExtractionProfile));
					Test->TestTrue(TEXT("Raid ticket retained"), PS->ExtractionRaid.IsValid());
					Test->TestEqual(TEXT("Squad survives lobby travel"), PS->ExtractionSquad, Squads.FindRef(PS->ExtractionProfile));
					if (!Current.IsValid()) Current = PS->ExtractionRaid;
					Test->TestEqual(TEXT("All players share raid"), PS->ExtractionRaid, Current);
					const auto* Rifle = PS->Inventory->Contents.InSlot(0);
					if (Test->TestNotNull(TEXT("Equipped rifle carried into raid"), Rifle))
						Test->TestEqual(TEXT("Same rifle instance"), Rifle->Id, Weapons.FindRef(PS->ExtractionProfile));
					Test->TestEqual(
						TEXT("Loadout deducted exactly once"), Store->View(PS->ExtractionProfile)->Loadout.Items.Num(),
						0);
				}
				Test->TestTrue(TEXT("New round has a fresh raid"), Current != LastRaid);
				LastRaid = Current;
				int32 Offset = -160;
				for (auto It = Server->GetPlayerControllerIterator(); It; ++It)
				{
					// Scene fixture only: the real exit timer and persistence path perform settlement.
					if (APawn* Pawn = It->Get()->GetPawn()) Pawn->SetActorLocation(FVector(1400 + Offset, 2900, 500));
					Offset += 160;
				}
				UE_LOG(LogTemp, Display, TEXT("EXTRACTION_PIE_LOBBY_ACTIVE Round=%d"), Round + 1);
				Advance(4);
				return false;
			}
			if (Stage == 4)
			{
				for (const auto& Pair : Weapons)
				{
					const auto* View = Store->View(Pair.Key);
					if (!View || View->Result.Raid != LastRaid || View->Result.Outcome != EExtractionOutcome::Extracted)
						return false;
				}
				Test->TestTrue(TEXT("Both players settled and raid closed"), Store->FindRaid(LastRaid)->Closed);
				UE_LOG(LogTemp, Display, TEXT("EXTRACTION_PIE_LOBBY_SETTLED Round=%d"), Round + 1);
				Advance(5);
				return false;
			}
			if (Stage == 5)
			{
				if (!Lobby || Lobby->PlayerArray.Num() != Players) return false;
				for (auto* Client : Clients)
				{
					if (!Client->GetGameState<ALobbyGameState>() || !ReadyButton(Client)) return false;
					const auto* Account = Client->GetFirstPlayerController()->FindComponentByClass<
						UExtractionAccountComponent>();
					if (!Account || !Account->Bound || Account->Profile.Result.Raid != LastRaid) return false;
					Test->TestTrue(
						TEXT("Same client account after return"), Weapons.Contains(Account->Profile.Profile));
				}
				for (APlayerState* State : Lobby->PlayerArray)
					Test->TestFalse(
						TEXT("Ready reset on return"), CastChecked<ABrokenEdgePlayerState>(State)->IsLobbyReady());
				for (const auto& Pair : Weapons)
				{
					Test->TestFalse(TEXT("Completed profile unlocked"), Store->IsInRaid(Pair.Key));
					Test->TestEqual(TEXT("Rifle quantity conserved"), Store->View(Pair.Key)->Stash.Count(TEXT("AK")),
					                3);
				}
				UE_LOG(LogTemp, Display, TEXT("EXTRACTION_PIE_LOBBY_RETURNED Round=%d"), ++Round);
				if (Round == 2) Stop();
				else Advance(0);
			}
			return false;
		}

	private:
		UButton* ReadyButton(UWorld* World) const
		{
			TArray<UUserWidget*> Widgets;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, ULobbyWidget::StaticClass(), true);
			if (Widgets.Num() != 1) return nullptr;
			auto* Button = Cast<UButton>(Widgets[0]->GetWidgetFromName(TEXT("ReadyButton")));
			return Button && Button->GetIsEnabled() ? Button : nullptr;
		}

		void Advance(int32 Next)
		{
			Stage = Next;
			Deadline = FPlatformTime::Seconds() + 120;
			Since = 0;
		}

		void Stop()
		{
			GEditor->RequestEndPlayMap();
			Advance(6);
		}

		FAutomationTestBase* Test;
		FString Mode;
		IConsoleVariable* Seamless = nullptr;
		int32 InitialSeamless = 0;
		int32 Stage = 0;
		int32 Round = 0;
		int32 Players = 2;
		TMap<FGuid, uint8> Squads;
		double Deadline = 0;
		double Since = 0;
		int64 BeforeBlocked = 0;
		TMap<FGuid, FGuid> Weapons;
		FGuid LastRaid;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionPIELobbyTest, "BrokenEdge.Extraction.PIE.Lobby",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionPIELobbyTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->PlayWorld)
	{
		AddError(TEXT("Run in an idle isolated editor"));
		return false;
	}
	const auto* Seamless = IConsoleManager::Get().FindConsoleVariable(TEXT("net.AllowPIESeamlessTravel"));
	if (!TestTrue(TEXT("Project config enables PIE seamless travel"), Seamless && Seamless->GetInt() == 1)) return
		false;
	FString Mode = TEXT("Extraction");
	FParse::Value(FCommandLine::Get(), TEXT("ExtractionPIEMode="), Mode);
	if (!FEditorFileUtils::LoadMap(TEXT("/Game/Maps/Lobby"), false, false)) return false;
	auto* Settings = NewObject<ULevelEditorPlaySettings>();
	Settings->bLaunchSeparateServer = false;
	int32 Players = 2;
	FParse::Value(FCommandLine::Get(), TEXT("ExtractionPIEPlayers="), Players);
	Settings->SetPlayNumberOfClients(FMath::Clamp(Players, 2, 3));
	Settings->SetRunUnderOneProcess(true);
	Settings->SetPlayNetMode(PIE_Client);
	Settings->NewWindowWidth = 640;
	Settings->NewWindowHeight = 360;
	FRequestPlaySessionParams Params;
	Params.EditorPlaySettings = Settings;
	Params.bAllowOnlineSubsystem = true;
	GEditor->RequestPlaySession(Params);
	ADD_LATENT_AUTOMATION_COMMAND(FExtractionPIELobbyFlow(this, Mode));
	return true;
}
#endif
