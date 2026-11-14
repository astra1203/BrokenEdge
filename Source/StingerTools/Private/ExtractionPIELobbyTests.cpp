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
#include "BrokenEdge/Extraction/Core/ExtractionAccountComponent.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/Core/ExtractionState.h"
#include "MultiplayerSessionsSubsystem.h"
#include "EngineUtils.h"
#include "BrokenEdge/Extraction/Map/ExtractionMapRegistry.h"
#include "BrokenEdge/Extraction/World/ExtractionExit.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/GameMode/BrokenEdgeGameMode.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"

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
			Deadline = FPlatformTime::Seconds() + 300;
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
			for (auto* Client : Clients)
				if (Client->IsInSeamlessTravel() || !Client->GetFirstPlayerController())
					return
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
				if (CrossMaps)
				{
					const FName Wanted = Round == 1 ? TEXT("SpaceCenter") : TEXT("SovietMilitary");
					if (SelectionRound != Round)
					{
						CastChecked<ABrokenEdgePlayerController>(Clients[0]->GetFirstPlayerController())->
							ServerSetExtractionMap(Wanted);
						SelectionRound = Round;
						return false;
					}
					if (Lobby->ExtractionMapId != Wanted) return false;
					for (auto* Client : Clients)
					{
						const auto* ClientLobby = Client->GetGameState<ALobbyGameState>();
						if (!ClientLobby || ClientLobby->ExtractionMapId != Wanted) return false;
					}
				}
				// Repeated starting-supply exports can fill the stash. Recover the same
				// rifle through the real overflow/economy RPCs before preparing the next raid.
				for (auto* Client : Clients)
				{
					auto* Account = Client->GetFirstPlayerController()->FindComponentByClass<UExtractionAccountComponent>();
					const auto& View = Account->Profile;
					if (PendingPreparation == View.Profile)
					{
						if (View.Revision <= PreparationRevision)
						{
							if (Now - PreparationStarted > 15)
							{
								Test->AddError(FString::Printf(TEXT("Overflow preparation RPC did not commit: %s"), *Account->Message));
								return true;
							}
							return false;
						}
						UE_LOG(LogTemp, Display, TEXT("EXTRACTION_PIE_LOBBY_PREPARATION_COMMITTED Action=%s Profile=%s Revision=%d"),
							*PreparationAction, *View.Profile.ToString(), View.Revision);
						PendingPreparation.Invalidate();
					}
					if (Round == 0 || View.Stash.Find(Weapons.FindRef(View.Profile))) continue;
					const auto* Overflow = View.Overflow.FindByPredicate([&](const auto& Item) { return Item.Id == Weapons.FindRef(View.Profile); });
					if (!Test->TestNotNull(TEXT("Returned rifle exists in overflow when stash is full"), Overflow)) return true;
					auto Placement = View.Stash;
					if (ExtractionItems::AutoPlace(Placement, *Overflow))
					{
						FExtractionStorageRequest Request;
						Request.From = EExtractionStorageSide::Overflow;
						Request.To = EExtractionStorageSide::Stash;
						Request.Move.Item = Overflow->Id;
						Request.Move.Quantity = 1;
						Request.Move.AutoPlace = true;
						Account->RequestMove(Request);
						PreparationAction = TEXT("ClaimOriginalRifle");
					}
					else
					{
						const auto* Surplus = View.Stash.Items.FindByPredicate([](const auto& Item)
						{
							return Item.Definition != TEXT("AK") && GetDefault<UExtractionProgressionRules>()->SellPrice(Item, Item.Quantity) > 0;
						});
						if (!Test->TestNotNull(TEXT("Saleable surplus can make room for overflow rifle"), Surplus)) return true;
						FExtractionEconomyRequest Request;
						Request.Action = EExtractionEconomyAction::Sell;
						Request.Item = Surplus->Id;
						Request.Quantity = Surplus->Quantity;
						Account->RequestEconomy(Request);
						PreparationAction = TEXT("SellSurplus");
					}
					PendingPreparation = View.Profile;
					PreparationRevision = View.Revision;
					PreparationStarted = Now;
					return false;
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
					Squads.FindOrAdd(Profile) = Client->GetFirstPlayerController()->GetPlayerState<
						ABrokenEdgePlayerState>()->ExtractionSquad;
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
					Advance(7);
					return false;
				}
				const auto* Game = Server->GetGameState<AExtractionGameState>();
				if (!Game || Game->Phase != EExtractionPhase::Active || Game->PlayerArray.Num() != Players)
					return
						false;
				for (auto* Client : Clients)
				{
					const auto* PS = Client->GetFirstPlayerController()->GetPlayerState<AExtractionPlayerState>();
					if (!PS || !PS->Inventory || !PS->Inventory->GetContents().InSlot(0)) return false;
				}
				if (!Game->MapDefinition) return false;
				for (auto* Client : Clients)
				{
					const auto* ClientGame = Client->GetGameState<AExtractionGameState>();
					if (!ClientGame || ClientGame->MapDefinition != Game->MapDefinition) return false;
				}
				if (CrossMaps)
					Test->TestEqual(TEXT("Selected map reaches all clients"), Game->MapDefinition->MapId,
					                FName(Round == 1 ? TEXT("SpaceCenter") : TEXT("SovietMilitary")));
				FGuid Current;
				for (APlayerState* State : Game->PlayerArray)
				{
					const auto* PS = Cast<AExtractionPlayerState>(State);
					if (!Test->TestNotNull(TEXT("Extraction PlayerState after seamless travel"), PS)) return true;
					Test->TestTrue(TEXT("Authenticated profile retained"), Weapons.Contains(PS->ExtractionProfile));
					Test->TestTrue(TEXT("Raid ticket retained"), PS->ExtractionRaid.IsValid());
					Test->TestEqual(TEXT("Squad survives lobby travel"), PS->ExtractionSquad,
					                Squads.FindRef(PS->ExtractionProfile));
					if (!Current.IsValid()) Current = PS->ExtractionRaid;
					Test->TestEqual(TEXT("All players share raid"), PS->ExtractionRaid, Current);
					// Current starting-supply rules may move the carried rifle into the backpack.
					const auto* Rifle = PS->Inventory->GetContents().Find(Weapons.FindRef(PS->ExtractionProfile));
					if (Test->TestNotNull(TEXT("Original rifle carried into raid"), Rifle))
						Test->TestEqual(TEXT("Same rifle instance"), Rifle->Id, Weapons.FindRef(PS->ExtractionProfile));
					Test->TestEqual(
						TEXT("Loadout deducted exactly once"), Store->View(PS->ExtractionProfile)->Loadout.Items.Num(),
						0);
				}
				Test->TestTrue(TEXT("New round has a fresh raid"), Current != LastRaid);
				LastRaid = Current;
				AExtractionExit* Exit = nullptr;
				for (TActorIterator<AExtractionExit> It(Server); It; ++It)
					if (!It->Definition.bRequireEmptyPack
						&& It->Definition.Capacity == 0)
					{
						Exit = *It;
						break;
					}
				if (!Test->TestNotNull(TEXT("Authored unlimited extraction zone"), Exit)) return true;
				if (Game->MapDefinition->MapId == TEXT("SpaceCenter") && !Exit->Available(
					Game->GetServerWorldTimeSeconds()))
				{
					// This test isolates cross-map travel and persistence. The original plaza has a 900 s gate
					// and switches, validated separately by its route scenario. Fixture only its runtime clock.
					Exit->RoundStartedAt = Game->GetServerWorldTimeSeconds() - Exit->Definition.OpensAfterSeconds - 1;
					Exit->SwitchedOpenAt = Game->GetServerWorldTimeSeconds();
					Exit->ForceNetUpdate();
					UE_LOG(LogTemp, Display, TEXT("EXTRACTION_PIE_LOBBY_EXIT_CLOCK_FIXTURE Map=SpaceCenter"));
				}
				if (!Test->TestTrue(
					TEXT("Prepared exit accepts extraction"),
					Exit->Available(Game->GetServerWorldTimeSeconds()))) return true;
				int32 Index = 0;
				for (auto It = Server->GetPlayerControllerIterator(); It; ++It)
				{
					// Scene fixture only: the real exit timer and persistence path perform settlement.
					if (APawn* Pawn = It->Get()->GetPawn())
						Pawn->SetActorLocation(
							Exit->GetActorLocation() + FVector((Index % 3 - 1) * 150, (Index / 3) * 150, 100));
					++Index;
				}
				UE_LOG(LogTemp, Display, TEXT("EXTRACTION_PIE_LOBBY_ACTIVE Round=%d"), Round + 1);
				Advance(4);
				return false;
			}
			if (Stage == 7)
			{
				auto* ClientPawn = Cast<ABrokenEdgeCharacter>(Clients[0]->GetFirstPlayerController()->GetPawn());
				if (!ClientPawn || !ClientPawn->GetPlayerState()) return false;
				LegacyPlayerId = ClientPawn->GetPlayerState()->GetPlayerId();
				ABrokenEdgeCharacter* Victim = nullptr;
				for (TActorIterator<ABrokenEdgeCharacter> It(Server); It; ++It)
					if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId() == LegacyPlayerId) Victim = *It;
				if (!Victim || !Victim->GetController()) return false;
				for (auto* Client : Clients)
				{
					bool Found = false;
					for (TActorIterator<ABrokenEdgeCharacter> It(Client); It; ++It)
						if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId() == LegacyPlayerId) Found = true;
					if (!Found) return false;
				}
				LegacyVictim = Victim; LegacyController = Victim->GetController(); LegacyState = Victim->GetPlayerState();
				const auto* Defaults = Victim->GetClass()->GetDefaultObject<ABrokenEdgeCharacter>();
				LegacySpawnHealth = Defaults->GetHealth(); LegacySpawnShield = Defaults->GetShield();
				auto* Game = Server->GetAuthGameMode<ABrokenEdgeGameMode>();
				if (!Test->TestNotNull(TEXT("Legacy production game mode"), Game)) return true;
				Test->TestEqual(TEXT("Missing victim does not crash damage calculation"), Game->CalculateDamage(Victim->GetController(), nullptr, 20), 20.f);
				// Environmental damage has no attacker; exercise the normal damage -> death -> respawn timer path.
				UGameplayStatics::ApplyDamage(Victim, Victim->GetHealth() + Victim->GetShield() + 1, nullptr, nullptr, nullptr);
				if (!Test->TestTrue(TEXT("Environmental damage defeats the legacy character"), Victim->IsDefeated())) return true;
				UE_LOG(LogTemp, Display, TEXT("EXTRACTION_PIE_LEGACY_DEFEATED Mode=%s Player=%d Pawn=%s"), *Mode, LegacyPlayerId, *Victim->GetName());
				Advance(8);
				Deadline = Now + 45;
				return false;
			}
			if (Stage == 8)
			{
				if (Now - LegacyReportAt > 5)
				{
					LegacyReportAt = Now;
					auto* Current = LegacyController.IsValid() ? Cast<ABrokenEdgeCharacter>(LegacyController->GetPawn()) : nullptr;
					UE_LOG(LogTemp, Display, TEXT("EXTRACTION_PIE_RESPAWN_WAIT Time=%.2f Old=%s Current=%s Defeated=%d HP=%.2f/%.2f"),
						Server->GetTimeSeconds(), *GetNameSafe(LegacyVictim.Get()), *GetNameSafe(Current), Current ? Current->IsDefeated() : -1,
						Current ? Current->GetHealth() : -1.f, Current ? Current->GetMaxHealth() : -1.f);
					for (auto* Client : Clients)
						for (TActorIterator<ABrokenEdgeCharacter> It(Client); It; ++It)
							UE_LOG(LogTemp, Display, TEXT("EXTRACTION_PIE_RESPAWN_COPY World=%s Pawn=%s Player=%d Defeated=%d HP=%.2f/%.2f"),
								*Client->GetName(), *It->GetName(), It->GetPlayerState() ? It->GetPlayerState()->GetPlayerId() : -1,
								It->IsDefeated(), It->GetHealth(), It->GetMaxHealth());
				}
				if (!LegacyController.IsValid()) { Test->AddError(TEXT("Legacy controller disappeared during respawn")); return true; }
				auto* Pawn = Cast<ABrokenEdgeCharacter>(LegacyController->GetPawn());
				if (!Pawn || Pawn == LegacyVictim.Get() || Pawn->IsDefeated()) return false;
				for (auto* Client : Clients)
				{
					ABrokenEdgeCharacter* Copy = nullptr;
					for (TActorIterator<ABrokenEdgeCharacter> It(Client); It; ++It)
						if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId() == LegacyPlayerId && !It->IsDefeated()) Copy = *It;
					if (!Copy || !FMath::IsNearlyEqual(Copy->GetHealth(), LegacySpawnHealth) || !FMath::IsNearlyEqual(Copy->GetShield(), LegacySpawnShield)) return false;
					if (Client->GetFirstPlayerController()->PlayerState->GetPlayerId() == LegacyPlayerId
						&& Client->GetFirstPlayerController()->GetPawn() != Copy) return false;
				}
				Test->TestFalse(TEXT("Normal respawn destroys the old pawn"), LegacyVictim.IsValid());
				Test->TestEqual(TEXT("Respawn retains PlayerState"), Pawn->GetPlayerState(), LegacyState.Get());
				Test->TestEqual(TEXT("Respawn restores authored initial health"), Pawn->GetHealth(), LegacySpawnHealth);
				Test->TestEqual(TEXT("Respawn restores authored initial shield"), Pawn->GetShield(), LegacySpawnShield);
				Test->TestFalse(TEXT("Respawn enables gameplay"), Pawn->bDisableGameplay);
				double NearestStart = TNumericLimits<double>::Max();
				for (TActorIterator<APlayerStart> It(Server); It; ++It)
					NearestStart = FMath::Min(NearestStart, FVector::Dist(Pawn->GetActorLocation(), It->GetActorLocation()));
				Test->TestTrue(TEXT("Respawn uses an authored PlayerStart"), NearestStart < 250);
				Test->TestFalse(TEXT("Respawn does not use the world origin"), Pawn->GetActorLocation().IsNearlyZero());
				Test->TestTrue(TEXT("Legacy session survives death and respawn"), Server->GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>()->HasSession());
				UE_LOG(LogTemp, Display, TEXT("EXTRACTION_PIE_LEGACY_RESPAWN_OK Mode=%s Player=%d Pawn=%s NearestStart=%.2f"), *Mode, LegacyPlayerId, *Pawn->GetName(), NearestStart);
				Stop();
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
					const auto* View = Store->View(Pair.Key);
					int32 TotalRifles = 0, OriginalInstances = 0;
					for (const auto* Items : {&View->Stash.Items, &View->Loadout.Items, &View->Overflow})
						for (const auto& Item : *Items)
						{
							if (Item.Definition == TEXT("AK")) TotalRifles += Item.Quantity;
							if (Item.Id == Pair.Value) OriginalInstances += Item.Quantity;
						}
					Test->TestEqual(TEXT("Rifle quantity conserved across stash loadout and overflow"), TotalRifles, 3);
					Test->TestEqual(TEXT("Original rifle returns exactly once"), OriginalInstances, 1);
				}
				UE_LOG(LogTemp, Display, TEXT("EXTRACTION_PIE_LOBBY_RETURNED Round=%d"), ++Round);
				if (Round == (CrossMaps ? 3 : 2)) Stop();
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
			Deadline = FPlatformTime::Seconds() + 300;
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
		int32 SelectionRound = -1;
		bool CrossMaps = FParse::Param(FCommandLine::Get(), TEXT("ExtractionPIECrossMaps"));
		int32 Players = 2;
		TMap<FGuid, uint8> Squads;
		double Deadline = 0;
		double Since = 0;
		int64 BeforeBlocked = 0;
		TMap<FGuid, FGuid> Weapons;
		FGuid PendingPreparation;
		int32 PreparationRevision = 0;
		double PreparationStarted = 0;
		FString PreparationAction;
		FGuid LastRaid;
		TWeakObjectPtr<ABrokenEdgeCharacter> LegacyVictim;
		TWeakObjectPtr<AController> LegacyController;
		TWeakObjectPtr<APlayerState> LegacyState;
		int32 LegacyPlayerId = INDEX_NONE;
		double LegacyReportAt = 0;
		float LegacySpawnHealth = 0, LegacySpawnShield = 0;
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
	if (!TestTrue(TEXT("Project config enables PIE seamless travel"), Seamless && Seamless->GetInt() == 1))
		return
			false;
	FString Mode = TEXT("Extraction");
	FParse::Value(FCommandLine::Get(), TEXT("ExtractionPIEMode="), Mode);
	if (!FEditorFileUtils::LoadMap(TEXT("/Game/Maps/Lobby"), false, false)) return false;
	auto* Settings = NewObject<ULevelEditorPlaySettings>();
	Settings->bLaunchSeparateServer = false;
	int32 Players = 2;
	FParse::Value(FCommandLine::Get(), TEXT("ExtractionPIEPlayers="), Players);
	Settings->SetPlayNumberOfClients(FMath::Clamp(Players, 2, 6));
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
