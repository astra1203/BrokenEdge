#include "LobbyGameMode.h"

#include "Menu.h"
#include "MultiplayerSessionsSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Misc/PackageName.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ALobbyGameMode::ALobbyGameMode()
{
	// 大厅尚未满员时，不让 AGameMode 自动开始比赛/在线会话。
	bDelayedStart = true;
	bUseSeamlessTravel = true;
	static ConstructorHelpers::FClassFinder<UMenu> MenuBlueprint(TEXT("/Game/Blueprints/UI/WBP_Menu"));
	if (MenuBlueprint.Succeeded()) MenuClass = MenuBlueprint.Class;
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
				Menu->MenuSetup(MenuNumPublicConnections, MenuMatchType, TEXT("/Game/Maps/Lobby"));
			}
		}
		return;
	}
	// 在登录初始化完成后判断人数，并允许开始会话失败后重新尝试。
	GetWorldTimerManager().SetTimer(ReadyCheckTimer, this, &ThisClass::TryStartGame, 1.f, true);
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
	if (!Sessions || !Sessions->HasSession() || Sessions->IsBusy() || !GameState || bStartingSession || bTravelRequested) return;
	if (GameState->PlayerArray.Num() < FMath::Max(1, Sessions->DesiredNumPublicConnections)) return;

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
	if (!GameState || GameState->PlayerArray.Num() < Sessions->DesiredNumPublicConnections) return;
	bTravelRequested = GetWorld()->ServerTravel(PendingMap + TEXT("?listen"));
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
