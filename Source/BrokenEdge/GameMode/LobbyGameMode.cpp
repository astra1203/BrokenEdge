#include "LobbyGameMode.h"

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

ALobbyGameMode::ALobbyGameMode()
{
	// 大厅尚未满员时，不让 AGameMode 自动开始比赛/在线会话。
	bDelayedStart = true;
	bUseSeamlessTravel = true;
	if (!IsRunningDedicatedServer())
	{
		static ConstructorHelpers::FClassFinder<UMenu> MenuBlueprint(TEXT("/Game/Blueprints/UI/WBP_Menu"));
		if (MenuBlueprint.Succeeded()) MenuClass = MenuBlueprint.Class;
	}
}

void ALobbyGameMode::BeginPlay()
{
	Super::BeginPlay();
	Sessions = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>() : nullptr;
	if (!Sessions) return;
	Sessions->MultiplayerOnStartSessionComplete.AddUniqueDynamic(this, &ThisClass::OnStartSession);

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
		GetWorldTimerManager().SetTimer(ReadyCheckTimer, this, &ThisClass::TryStartGame, 1.f, true);
	}
}

void ALobbyGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	if (GameState)
	{
		UE_LOG(LogTemp, Log, TEXT("Lobby: Players=%d"), GameState->PlayerArray.Num());
	}
}

void ALobbyGameMode::TryStartGame()
{
	if (GetNetMode() != NM_DedicatedServer || !Sessions || Sessions->IsBusy() || !GameState || bStartingSession || bTravelRequested) return;
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
	// 专服本身不算玩家；开局人数与最大容量分开配置。
	if (!Sessions->ShouldAutoStartSession() || GameState->PlayerArray.Num() < Sessions->GetMinPlayersToStart()) return;

	if (Sessions->DesiredMatchType == TEXT("FreeForAll")) PendingMap = TEXT("/Game/Maps/FreeForAll");
	else if (Sessions->DesiredMatchType == TEXT("Teams")) PendingMap = TEXT("/Game/Maps/Teams");
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Lobby: unsupported MatchType '%s'"), *Sessions->DesiredMatchType);
		GetWorldTimerManager().ClearTimer(ReadyCheckTimer);
		return;
	}
	if (!FPackageName::DoesPackageExist(PendingMap))
	{
		UE_LOG(LogTemp, Error, TEXT("Lobby: map not found '%s'"), *PendingMap);
		GetWorldTimerManager().ClearTimer(ReadyCheckTimer);
		return;
	}
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
		return;
	}
	// 等待异步开始期间有人离开时，继续等人；StartSession 的重复调用是幂等的。
	if (!GameState || GameState->PlayerArray.Num() < Sessions->GetMinPlayersToStart()) return;
	// 将容量传给下一张地图的 GameSession，直接 IP 连接也受人数限制。
	bTravelRequested = GetWorld()->ServerTravel(FString::Printf(TEXT("%s?MaxPlayers=%d"),
		*PendingMap, Sessions->DesiredNumPublicConnections));
	UE_LOG(LogTemp, Log, TEXT("Lobby travel: Map=%s Players=%d Accepted=%d"),
		*PendingMap, GameState->PlayerArray.Num(), bTravelRequested);
	if (bTravelRequested) GetWorldTimerManager().ClearTimer(ReadyCheckTimer);
}

void ALobbyGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ReadyCheckTimer);
	if (Sessions) Sessions->MultiplayerOnStartSessionComplete.RemoveDynamic(this, &ThisClass::OnStartSession);
	Super::EndPlay(EndPlayReason);
}
