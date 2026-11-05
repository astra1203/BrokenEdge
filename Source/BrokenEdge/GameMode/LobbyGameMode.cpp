#include "LobbyGameMode.h"
#include "BrokenEdge/Extraction/ExtractionMapRegistry.h"
#include "SessionModes.h"

#include "Menu.h"
#include "MultiplayerSessionsSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerController.h"
#include "Misc/PackageName.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "BrokenEdge/GameState/LobbyGameState.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "BrokenEdge/PlayerState/BrokenEdgePlayerState.h"
#include "BrokenEdge/Extraction/ExtractionAccountComponent.h"

ALobbyGameMode::ALobbyGameMode()
{
	// 准备流程统一控制在线会话和跳图，不让 AGameMode 自动开局。
	bDelayedStart = true;
	bUseSeamlessTravel = true;
	GameStateClass = ALobbyGameState::StaticClass();
	PlayerStateClass = ABrokenEdgePlayerState::StaticClass();
	MenuClass = UMenu::StaticClass();
	if (!IsRunningDedicatedServer())
	{
		static ConstructorHelpers::FClassFinder<UMenu> MenuBlueprint(TEXT("/Game/Blueprints/UI/WBP_Menu"));
		if (MenuBlueprint.Succeeded()) MenuClass = MenuBlueprint.Class;
	}
}

void ALobbyGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	// 已有 BP_LobbyGameMode 保存了 Engine.GameState/PlayerState 的旧默认值。
	// 在生成实例之前保证大厅网络数据类型，同时保留项目控制器蓝图。
	GameStateClass = ALobbyGameState::StaticClass();
	if (!PlayerStateClass || !PlayerStateClass->IsChildOf(ABrokenEdgePlayerState::StaticClass()))
		PlayerStateClass = ABrokenEdgePlayerState::StaticClass();
	if (!PlayerControllerClass || !PlayerControllerClass->IsChildOf(ABrokenEdgePlayerController::StaticClass()))
		PlayerControllerClass = ABrokenEdgePlayerController::StaticClass();
	Super::InitGame(MapName, Options, ErrorMessage);
}

void ALobbyGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
                              FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (bTravelRequested || bStartingSession) ErrorMessage = TEXT("MATCH_STARTING");
	if (Sessions && Sessions->DesiredMatchType == TEXT("Extraction") && GetNumPlayers() >= 6) ErrorMessage = TEXT("ROOM_FULL");
}

void ALobbyGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (auto* Registry = GetGameInstance()->GetSubsystem<UExtractionMapRegistry>())
		if (auto* GS = GetGameState<ALobbyGameState>()) GS->ExtractionMapId = Registry->SelectedMap;
	Sessions = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>() : nullptr;
	if (!Sessions) return;
	Sessions->MultiplayerOnStartSessionComplete.AddUniqueDynamic(this, &ThisClass::OnStartSession);
	Sessions->MultiplayerOnUpdateMatchTypeComplete.AddUObject(this, &ThisClass::OnMatchTypeUpdated);

	if (GetNetMode() == NM_Standalone && !Sessions->HasSession())
	{
		APlayerController* Player = GetWorld()->GetFirstPlayerController();
		if (Player && Player->IsLocalController() && MenuClass)
		{
			if (UMenu* Menu = CreateWidget<UMenu>(Player, MenuClass))
			{
				FParse::Value(FCommandLine::Get(), TEXT("MatchType="), MenuMatchType);
				Menu->MenuSetup(MenuNumPublicConnections, MenuMatchType, TEXT("/Game/Maps/Lobby"));
			}
		}
		return;
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		// Only the authoritative server may recover this store, never an offline client browser.
		auto* Store = GetGameInstance()->GetSubsystem<UExtractionPersistence>();
		bPreparedRaidsCleared = Store && Store->AbortPreparedRaids();
		Sessions->InitializeDedicatedServer();
		if (GameSession) GameSession->MaxPlayers = Sessions->DesiredNumPublicConnections;
		NextSessionRetryTime = GetWorld()->GetTimeSeconds() + 5.0;
		GetWorldTimerManager().SetTimer(ReadyCheckTimer, this, &ThisClass::TryStartGame, 0.25f, true);
	}
}

void ALobbyGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	if (ABrokenEdgePlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<ABrokenEdgePlayerState>() : nullptr)
		PS->SetLobbyReady(false);
	AssignExtractionSquads();
	CancelCountdown();
	if (GameState)
	{
		UE_LOG(LogTemp, Log, TEXT("Lobby: Players=%d"), GameState->PlayerArray.Num());
	}
}

void ALobbyGameMode::Logout(AController* Exiting)
{
	if (ABrokenEdgePlayerState* PS = Exiting ? Exiting->GetPlayerState<ABrokenEdgePlayerState>() : nullptr)
		PS->SetLobbyReady(false);
	CancelCountdown();
	Super::Logout(Exiting);
}

void ALobbyGameMode::AssignExtractionSquads()
{
	if (!GameState || !Sessions || Sessions->DesiredMatchType != TEXT("Extraction")) return;
	int32 Counts[3] = {};
	TArray<ABrokenEdgePlayerState*> Pending;
	for (APlayerState* State : GameState->PlayerArray)
		if (auto* PS = Cast<ABrokenEdgePlayerState>(State); PS && !PS->IsInactive() && !PS->IsOnlyASpectator())
		{
			if (PS->ExtractionSquad >= 1 && PS->ExtractionSquad <= 2 && Counts[PS->ExtractionSquad] < 3) ++Counts[PS->ExtractionSquad];
			else Pending.Add(PS);
		}
	Pending.Sort([](const auto& A, const auto& B) { return A.GetPlayerId() < B.GetPlayerId(); });
	for (auto* PS : Pending)
	{
		const uint8 Squad = Counts[1] <= Counts[2] ? 1 : 2;
		PS->ExtractionSquad = Counts[Squad] < 3 ? Squad : 0;
		if (PS->ExtractionSquad) ++Counts[Squad];
		PS->ForceNetUpdate();
	}
}

void ALobbyGameMode::SetExtractionSquad(APlayerController* Player, uint8 Squad)
{
	auto* GS = GetGameState<ALobbyGameState>();
	auto* PS = Player ? Player->GetPlayerState<ABrokenEdgePlayerState>() : nullptr;
	if (!Sessions || Sessions->DesiredMatchType != TEXT("Extraction") || !GS || !PS || Squad < 1 || Squad > 2
		|| PS->GetOwner() != Player || !GS->PlayerArray.Contains(PS) || PS->IsLobbyReady() || PS->IsInactive()
		|| PS->IsOnlyASpectator() || bTravelRequested || bStartingSession || GS->bModeUpdating || GS->CountdownEndTime > 0) return;
	int32 Count = 0;
	for (APlayerState* State : GS->PlayerArray)
		if (auto* Other = Cast<ABrokenEdgePlayerState>(State); Other && Other != PS && !Other->IsInactive() && Other->ExtractionSquad == Squad) ++Count;
	if (Count >= 3) return;
	PS->ExtractionSquad = Squad;
	PS->ForceNetUpdate();
}

void ALobbyGameMode::SetPlayerReady(APlayerController* Player, bool bReady)
{
	if (bTravelRequested || !Player || !GameState || GetNetMode() == NM_Standalone) return;
	if (const ALobbyGameState* GS = GetGameState<ALobbyGameState>(); GS && GS->bModeUpdating) return;
	ABrokenEdgePlayerState* PS = Player->GetPlayerState<ABrokenEdgePlayerState>();
	if (!PS || PS->GetOwner() != Player || !GameState->PlayerArray.Contains(PS)
		|| PS->IsInactive() || PS->IsOnlyASpectator())
		return;
	if (bReady && Sessions && Sessions->DesiredMatchType == TEXT("Extraction"))
	{
		auto* Account = Player->FindComponentByClass<UExtractionAccountComponent>();
		if (!bPreparedRaidsCleared || !Account || !Account->Bound || !Account->Store()
			|| !Account->Store()->IsAvailable()) return;
	}
	PS->SetLobbyReady(bReady);
	UE_LOG(LogTemp, Log, TEXT("Lobby ready: Player=%s Ready=%d"), *PS->GetPlayerName(), bReady);
	if (!bReady) CancelCountdown();
}

void ALobbyGameMode::SetMatchType(APlayerController* Player, const FString& MatchType)
{
	ALobbyGameState* GS = GetGameState<ALobbyGameState>();
	if (GetNetMode() != NM_DedicatedServer || !Player || !GS || !Sessions
		|| bTravelRequested || bStartingSession || GS->bModeUpdating || Sessions->IsBusy()
		|| !Sessions->HasSession() || MatchType == Sessions->DesiredMatchType
		|| !SessionModes::IsSupported(MatchType))
		return;
	const ABrokenEdgePlayerState* PS = Player->GetPlayerState<ABrokenEdgePlayerState>();
	if (!PS || PS->GetOwner() != Player || !GS->PlayerArray.Contains(PS)
		|| PS->IsInactive() || PS->IsOnlyASpectator() || PS->IsLobbyReady())
		return;
	if (MatchType == TEXT("Extraction") && GetNumPlayers() > 6) return;
	CancelCountdown();
	GS->bModeUpdating = true;
	GS->LobbyStatus = TEXT("正在切换游戏模式…");
	GS->ForceNetUpdate();
	Sessions->UpdateMatchType(MatchType);
}

void ALobbyGameMode::OnMatchTypeUpdated(bool bWasSuccessful)
{
	ALobbyGameState* GS = GetGameState<ALobbyGameState>();
	if (!GS || !GS->bModeUpdating) return;
	GS->bModeUpdating = false;
	if (bWasSuccessful)
	{
		GS->RoomMatchType = Sessions->DesiredMatchType;
		for (APlayerState* State : GS->PlayerArray)
			if (ABrokenEdgePlayerState* PS = Cast<ABrokenEdgePlayerState>(State)) PS->SetLobbyReady(false);
		AssignExtractionSquads();
		if (GameSession) GameSession->MaxPlayers = Sessions->DesiredNumPublicConnections;
		PendingMap.Reset();
		GS->LobbyStatus = TEXT("模式已切换，请所有玩家重新准备");
	}
	else GS->LobbyStatus = TEXT("模式切换失败，请重试");
	GS->ForceNetUpdate();
}

bool ALobbyGameMode::AreAllPlayersReady() const
{
	if (!GameState || !Sessions) return false;
	int32 Players = 0;
	int32 Squads[3] = {};
	for (const APlayerState* State : GameState->PlayerArray)
	{
		if (!IsValid(State) || State->IsInactive() || State->IsOnlyASpectator()) continue;
		++Players;
		const ABrokenEdgePlayerState* PS = Cast<ABrokenEdgePlayerState>(State);
		if (!PS || !PS->IsLobbyReady()) return false;
		if (Sessions->DesiredMatchType == TEXT("Extraction"))
		{
			if (!PS->ExtractionProfile.IsValid() || PS->ExtractionSquad < 1 || PS->ExtractionSquad > 2) return false;
			if (++Squads[PS->ExtractionSquad] > 3 || Players > 6) return false;
		}
	}
	return Players >= Sessions->GetMinPlayersToStart();
}

void ALobbyGameMode::CancelCountdown()
{
	if (ALobbyGameState* GS = GetGameState<ALobbyGameState>())
	{
		if (GS->CountdownEndTime <= 0.0) return;
		UE_LOG(LogTemp, Log, TEXT("Lobby countdown cancelled"));
		GS->CountdownEndTime = 0.0;
		GS->ForceNetUpdate();
	}
}

FString ALobbyGameMode::ExtractionStartError() const
{
	if (!Sessions || Sessions->DesiredMatchType != TEXT("Extraction")) return {};
	const auto* Registry = GetGameInstance()->GetSubsystem<UExtractionMapRegistry>();
	FString MapError;
	if (!Registry || !Registry->Validate(Registry->SelectedMap, MapError))
		return MapError.IsEmpty() ? TEXT("地图注册表不可用") : MapError;
	const auto* Store = GetGameInstance()->GetSubsystem<UExtractionPersistence>();
	if (!bPreparedRaidsCleared || !Store || !Store->IsAvailable())
		return Store && !Store->Error.IsEmpty() ? Store->Error : TEXT("仓库恢复未完成，暂时无法开局");
#if WITH_EDITOR
	const auto* Seamless = IConsoleManager::Get().FindConsoleVariable(TEXT("net.AllowPIESeamlessTravel"));
	if (GetWorld()->WorldType == EWorldType::PIE && !FParse::Param(FCommandLine::Get(), TEXT("MultiprocessOSS"))
		&& (!Seamless || Seamless->GetInt() == 0))
		return TEXT("PIE 未启用无缝切图，无法开始撤离对局");
#endif
	return {};
}

void ALobbyGameMode::TryStartGame()
{
	if (GetNetMode() != NM_DedicatedServer || !Sessions || !GameState || bTravelRequested) return;
	ALobbyGameState* GS = GetGameState<ALobbyGameState>();
	if (!GS) return;
	AssignExtractionSquads();
	GS->RoomMatchType = Sessions->DesiredMatchType;
	GS->MinimumPlayers = Sessions->GetMinPlayersToStart();
	GS->Capacity = Sessions->DesiredNumPublicConnections;
	const FString StartError = ExtractionStartError();
	if (!StartError.IsEmpty())
	{
		CancelCountdown();
		GS->LobbyStatus = StartError;
		return;
	}
	if (!AreAllPlayersReady()) CancelCountdown();
	if (Sessions->IsBusy() || bStartingSession) return;
	if (!Sessions->HasSession())
	{
		if (GetWorld()->GetTimeSeconds() >= NextSessionRetryTime)
		{
			NextSessionRetryTime = GetWorld()->GetTimeSeconds() + 5.0;
			Sessions->InitializeDedicatedServer();
			if (GameSession) GameSession->MaxPlayers = Sessions->DesiredNumPublicConnections;
		}
		return;
	}
	// 专服本身不算玩家；人数达标且全员准备，才允许开始倒计时。
	if (!Sessions->ShouldAutoStartSession())
	{
		GS->LobbyStatus = TEXT("服务器已关闭自动开局");
		return;
	}
	if (!AreAllPlayersReady()) return;

	PendingMap = SessionModes::Map(Sessions->DesiredMatchType);
	if (Sessions->DesiredMatchType == TEXT("Extraction"))
	{
		const auto* Registry = GetGameInstance()->GetSubsystem<UExtractionMapRegistry>();
		const auto* D = Registry ? Registry->Find(Registry->SelectedMap) : nullptr;
		PendingMap = D ? D->Level.ToSoftObjectPath().GetLongPackageName() : FString();
	}
	if (PendingMap.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Lobby: unsupported MatchType '%s'"), *Sessions->DesiredMatchType);
		GS->LobbyStatus = TEXT("服务器游戏模式无效，请联系房主");
		GetWorldTimerManager().ClearTimer(ReadyCheckTimer);
		return;
	}
	if (!FPackageName::DoesPackageExist(PendingMap))
	{
		UE_LOG(LogTemp, Error, TEXT("Lobby: map not found '%s'"), *PendingMap);
		GS->LobbyStatus = TEXT("服务器缺少游戏地图，暂时无法开局");
		GetWorldTimerManager().ClearTimer(ReadyCheckTimer);
		return;
	}
	if (GS->CountdownEndTime <= 0.0)
	{
		GS->CountdownEndTime = GS->GetServerWorldTimeSeconds() + 3.0;
		GS->LobbyStatus.Reset();
		GS->ForceNetUpdate();
		UE_LOG(LogTemp, Log, TEXT("Lobby countdown started: Players=%d"), GameState->PlayerArray.Num());
		return;
	}
	if (GS->GetServerWorldTimeSeconds() < GS->CountdownEndTime) return;
	bStartingSession = true;
	Sessions->StartSession();
}

void ALobbyGameMode::OnStartSession(bool bWasSuccessful)
{
	if (!bStartingSession) return;
	bStartingSession = false;
	if (!bWasSuccessful)
	{
		UE_LOG(LogTemp, Warning, TEXT("Lobby: StartSession failed; will retry."));
		CancelCountdown();
		if (ALobbyGameState* GS = GetGameState<ALobbyGameState>()) GS->LobbyStatus = TEXT("开始会话失败，正在重试");
		return;
	}
	// 异步开始期间可能取消准备或有玩家加入；重新校验，不使用请求时的快照。
	ALobbyGameState* GS = GetGameState<ALobbyGameState>();
	if (!GS || !AreAllPlayersReady() || GS->CountdownEndTime <= 0.0
		|| GS->GetServerWorldTimeSeconds() < GS->CountdownEndTime)
		return;
	const FString StartError = ExtractionStartError();
	if (!StartError.IsEmpty())
	{
		GS->LobbyStatus = StartError;
		CancelCountdown();
		return;
	}
	// 将容量传给下一张地图的 GameSession，直接 IP 连接也受人数限制。
	FGuid Raid;
	UExtractionPersistence* Store = nullptr;
	if (Sessions->DesiredMatchType == TEXT("Extraction"))
	{
		Store = GetGameInstance()->GetSubsystem<UExtractionPersistence>();
		TArray<FGuid> Profiles;
		for (APlayerState* State : GS->PlayerArray) if (auto* PS = Cast<ABrokenEdgePlayerState>(State)) Profiles.Add(
			PS->ExtractionProfile);
		if (!Store->BeginRaid(Profiles, Raid))
		{
			GS->LobbyStatus = Store->Error;
			CancelCountdown();
			return;
		}
		for (APlayerState* State : GS->PlayerArray) if (auto* PS = Cast<ABrokenEdgePlayerState>(State)) PS->ExtractionRaid =
			Raid;
	}
	bTravelRequested = GetWorld()->ServerTravel(FString::Printf(TEXT("%s?MaxPlayers=%d?ExpectedPlayers=%d"),
	                                                            *PendingMap, Sessions->DesiredNumPublicConnections,
	                                                            GameState->PlayerArray.Num()));
	GS->bTravelling = bTravelRequested;
	GS->ForceNetUpdate();
	if (!bTravelRequested)
	{
		CancelCountdown();
		const bool Refunded = !Store || Store->AbortRaid(Raid);
		GS->LobbyStatus = Refunded ? TEXT("地图跳转失败，出战已取消，请重新准备") : Store->Error;
		for (APlayerState* State : GS->PlayerArray)
			if (auto* PS = Cast<ABrokenEdgePlayerState>(State))
			{
				PS->SetLobbyReady(false);
				if (Refunded) PS->ExtractionRaid.Invalidate();
			}
	}
	UE_LOG(LogTemp, Log, TEXT("Lobby travel: Map=%s Players=%d Accepted=%d"),
	       *PendingMap, GameState->PlayerArray.Num(), bTravelRequested);
	if (bTravelRequested) GetWorldTimerManager().ClearTimer(ReadyCheckTimer);
}

void ALobbyGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ReadyCheckTimer);
	if (Sessions) Sessions->MultiplayerOnUpdateMatchTypeComplete.RemoveAll(this);
	if (Sessions) Sessions->MultiplayerOnStartSessionComplete.RemoveDynamic(this, &ThisClass::OnStartSession);
	Super::EndPlay(EndPlayReason);
}

void ALobbyGameMode::SetExtractionMap(APlayerController* Player, FName MapId)
{
	auto* GS = GetGameState<ALobbyGameState>();
	auto* Registry = GetGameInstance()->GetSubsystem<UExtractionMapRegistry>();
	if (GetNetMode() != NM_DedicatedServer || !Player || !GS || !Registry || !Sessions
		|| Sessions->DesiredMatchType != TEXT("Extraction") || bStartingSession || bTravelRequested
		|| GS->bModeUpdating || Sessions->IsBusy() || !Sessions->HasSession() || Registry->SelectedMap == MapId) return;
	const auto* PS = Player->GetPlayerState<ABrokenEdgePlayerState>();
	if (!PS || PS->GetOwner() != Player || !GS->PlayerArray.Contains(PS) || PS->IsInactive()
		|| PS->IsOnlyASpectator() || PS->IsLobbyReady()) return;
	FString Error;
	if (!Registry->Validate(MapId, Error)) { GS->LobbyStatus = Error; return; }
	CancelCountdown();
	Registry->SelectedMap = MapId;
	GS->ExtractionMapId = MapId;
	PendingMap.Reset();
	for (APlayerState* State : GS->PlayerArray)
		if (auto* Member = Cast<ABrokenEdgePlayerState>(State)) Member->SetLobbyReady(false);
	GS->LobbyStatus = TEXT("地图已切换，请所有玩家重新准备");
	GS->ForceNetUpdate();
}
