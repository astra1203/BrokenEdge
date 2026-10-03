#include "LobbyGameMode.h"
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
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "BrokenEdge/GameState/LobbyGameState.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "BrokenEdge/PlayerState/BrokenEdgePlayerState.h"

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
	if (bTravelRequested) ErrorMessage = TEXT("MATCH_STARTING");
}

void ALobbyGameMode::BeginPlay()
{
	Super::BeginPlay();
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

void ALobbyGameMode::SetPlayerReady(APlayerController* Player, bool bReady)
{
	if (bTravelRequested || !Player || !GameState || GetNetMode() == NM_Standalone) return;
	if (const ALobbyGameState* GS = GetGameState<ALobbyGameState>(); GS && GS->bModeUpdating) return;
	ABrokenEdgePlayerState* PS = Player->GetPlayerState<ABrokenEdgePlayerState>();
	if (!PS || PS->GetOwner() != Player || !GameState->PlayerArray.Contains(PS)
		|| PS->IsInactive() || PS->IsOnlyASpectator())
		return;
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
	for (const APlayerState* State : GameState->PlayerArray)
	{
		if (!IsValid(State) || State->IsInactive() || State->IsOnlyASpectator()) continue;
		++Players;
		const ABrokenEdgePlayerState* PS = Cast<ABrokenEdgePlayerState>(State);
		if (!PS || !PS->IsLobbyReady()) return false;
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

void ALobbyGameMode::TryStartGame()
{
	if (GetNetMode() != NM_DedicatedServer || !Sessions || !GameState || bTravelRequested) return;
	ALobbyGameState* GS = GetGameState<ALobbyGameState>();
	if (!GS) return;
	GS->RoomMatchType = Sessions->DesiredMatchType;
	GS->MinimumPlayers = Sessions->GetMinPlayersToStart();
	GS->Capacity = Sessions->DesiredNumPublicConnections;
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
	// 将容量传给下一张地图的 GameSession，直接 IP 连接也受人数限制。
	bTravelRequested = GetWorld()->ServerTravel(FString::Printf(TEXT("%s?MaxPlayers=%d?ExpectedPlayers=%d"),
	                                                            *PendingMap, Sessions->DesiredNumPublicConnections,
	                                                            GameState->PlayerArray.Num()));
	GS->bTravelling = bTravelRequested;
	GS->ForceNetUpdate();
	if (!bTravelRequested)
	{
		CancelCountdown();
		GS->LobbyStatus = TEXT("地图跳转失败，正在重试");
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
