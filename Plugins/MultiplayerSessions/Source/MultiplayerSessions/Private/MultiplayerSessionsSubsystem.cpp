#include "MultiplayerSessionsSubsystem.h"
#include "SessionModes.h"

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "MultiplayerSessions.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemUtils.h"
#include "Online/OnlineSessionNames.h"

UMultiplayerSessionsSubsystem::UMultiplayerSessionsSubsystem()
	: CreateSessionCompleteDelegate(
		  FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnCreateSessionComplete)),
	  FindSessionsCompleteDelegate(
		  FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::OnFindSessionsComplete)),
	  JoinSessionCompleteDelegate(
		  FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnJoinSessionComplete)),
	  DestorySessionCompleteDelegate(
		  FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::OnDestorySessionComplete)),
	  StartSessionCompleteDelegate(
		  FOnStartSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnStartSessionComplete))
{
}

void UMultiplayerSessionsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RefreshSessionInterface();
}

void UMultiplayerSessionsSubsystem::Deinitialize()
{
	ClearOnlineDelegates();
	LastSessionSearch.Reset();
	LastSessionSettings.Reset();
	SessionInterface.Reset();
	bCreateSessionOnDestory = false;
	Super::Deinitialize();
}

bool UMultiplayerSessionsSubsystem::IsDedicatedServerInstance() const
{
	return GetWorld() && GetWorld()->GetNetMode() == NM_DedicatedServer;
}

bool UMultiplayerSessionsSubsystem::TryGetLocalPlayerId(FUniqueNetIdRepl& OutId) const
{
	const ULocalPlayer* Player = GetGameInstance() ? GetGameInstance()->GetFirstGamePlayer() : nullptr;
	if (!Player) return false;
	OutId = Player->GetPreferredUniqueNetId();
	return OutId.IsValid();
}

bool UMultiplayerSessionsSubsystem::IsBusy() const
{
	return CreateSessionCompleteDelegateHandle.IsValid()
		|| FindSessionsCompleteDelegateHandle.IsValid()
		|| JoinSessionCompleteDelegateHandle.IsValid()
		|| DestorySessionCompleteDelegateHandle.IsValid()
		|| StartSessionCompleteDelegateHandle.IsValid()
		|| EndSessionCompleteDelegateHandle.IsValid()
		|| UpdateSessionCompleteDelegateHandle.IsValid();
}

bool UMultiplayerSessionsSubsystem::HasSession() const
{
	return SessionInterface.IsValid() && SessionInterface->GetNamedSession(NAME_GameSession) != nullptr;
}

bool UMultiplayerSessionsSubsystem::GetResolvedConnectString(FString& Address) const
{
	Address.Reset();
	return SessionInterface.IsValid() && SessionInterface->GetResolvedConnectString(NAME_GameSession, Address)
		&& !Address.IsEmpty();
}

bool UMultiplayerSessionsSubsystem::RefreshSessionInterface()
{
	if (!GetWorld()) return false;
	const IOnlineSessionPtr WorldInterface = Online::GetSessionInterface(GetWorld());
	if (WorldInterface != SessionInterface)
	{
		// 不得在异步请求中途把完成回调切换到另一个 PIE 实例。
		if (IsBusy()) return false;
		ClearOnlineDelegates();
		SessionInterface = WorldInterface;
	}
	return SessionInterface.IsValid();
}

void UMultiplayerSessionsSubsystem::ClearOnlineDelegates()
{
	if (SessionInterface.IsValid())
	{
		SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
		SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
		SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
		SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestorySessionCompleteDelegateHandle);
		SessionInterface->ClearOnStartSessionCompleteDelegate_Handle(StartSessionCompleteDelegateHandle);
		SessionInterface->ClearOnEndSessionCompleteDelegate_Handle(EndSessionCompleteDelegateHandle);
		SessionInterface->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateSessionCompleteDelegateHandle);
	}
	CreateSessionCompleteDelegateHandle.Reset();
	FindSessionsCompleteDelegateHandle.Reset();
	JoinSessionCompleteDelegateHandle.Reset();
	DestorySessionCompleteDelegateHandle.Reset();
	StartSessionCompleteDelegateHandle.Reset();
	EndSessionCompleteDelegateHandle.Reset();
	UpdateSessionCompleteDelegateHandle.Reset();
	PendingMatchType.Reset();
}

void UMultiplayerSessionsSubsystem::UpdateMatchType(const FString& MatchType)
{
	if (IsBusy()) return;
	if (!IsDedicatedServerInstance() || !RefreshSessionInterface() || !HasSession()
		|| !SessionModes::IsSupported(MatchType))
	{
		MultiplayerOnUpdateMatchTypeComplete.Broadcast(false);
		return;
	}
	FOnlineSessionSettings Settings = SessionInterface->GetNamedSession(NAME_GameSession)->SessionSettings;
	Settings.Set(MultiplayerSessionKeys::MatchType, MatchType, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.bAllowJoinInProgress = MatchType != TEXT("Extraction");
	// 自动生成的房名跟随模式；命令行指定的房名保持其自定义含义。
	if (!FParse::Param(FCommandLine::Get(), TEXT("RoomName")))
	{
		FString CustomRoomName;
		if (!FParse::Value(FCommandLine::Get(), TEXT("RoomName="), CustomRoomName))
			Settings.Set(FName(TEXT("RoomName")), FString::Printf(TEXT("BrokenEdge %s"), *MatchType),
			             EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	}
	PendingMatchType = MatchType;
	UpdateSessionCompleteDelegateHandle = SessionInterface->AddOnUpdateSessionCompleteDelegate_Handle(
		FOnUpdateSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnUpdateMatchTypeComplete));
	if (!SessionInterface->UpdateSession(NAME_GameSession, Settings, true) && UpdateSessionCompleteDelegateHandle.
		IsValid())
		OnUpdateMatchTypeComplete(NAME_GameSession, false);
}

void UMultiplayerSessionsSubsystem::OnUpdateMatchTypeComplete(FName SessionName, bool bWasSuccessful)
{
	if (SessionName != NAME_GameSession) return;
	if (SessionInterface) SessionInterface->ClearOnUpdateSessionCompleteDelegate_Handle(
		UpdateSessionCompleteDelegateHandle);
	UpdateSessionCompleteDelegateHandle.Reset();
	if (bWasSuccessful)
	{
		DesiredMatchType = PendingMatchType;
		if (const FNamedOnlineSession* Session = SessionInterface->GetNamedSession(NAME_GameSession))
			LastSessionSettings = MakeShared<FOnlineSessionSettings>(Session->SessionSettings);
	}
	PendingMatchType.Reset();
	UE_LOG(LogMultiplayerSessions, Log, TEXT("Update mode complete: Success=%d MatchType=%s"), bWasSuccessful,
	       *DesiredMatchType);
	MultiplayerOnUpdateMatchTypeComplete.Broadcast(bWasSuccessful);
}

void UMultiplayerSessionsSubsystem::CreateSession(int32 NumPublicConnections, FString MatchType)
{
	// 忽略重复请求，不让额外失败事件提前解锁等待原请求的 UI。
	if (IsBusy()) return;
	const bool bDedicated = IsDedicatedServerInstance();
	// 客户端不再提供监听建房；专服不存在 ULocalPlayer。
	if (!bDedicated || !RefreshSessionInterface() || MatchType.TrimStartAndEnd().IsEmpty())
	{
		MultiplayerOnCreateSessionComplete.Broadcast(false);
		return;
	}
	DesiredNumPublicConnections = FMath::Clamp(NumPublicConnections, 1, 64);
	DesiredMatchType = MatchType.TrimStartAndEnd();
	if (HasSession())
	{
		bCreateSessionOnDestory = true;
		LastNumPublicConnections = DesiredNumPublicConnections;
		LastMatchType = DesiredMatchType;
		DestorySession();
		return;
	}

	const IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetWorld());
	LastSessionSettings = MakeShared<FOnlineSessionSettings>();
	LastSessionSettings->bIsLANMatch = Subsystem && Subsystem->GetSubsystemName() == NULL_SUBSYSTEM;
	LastSessionSettings->bIsDedicated = true;
	LastSessionSettings->NumPublicConnections = DesiredNumPublicConnections;
	LastSessionSettings->bAllowJoinInProgress = DesiredMatchType != TEXT("Extraction");
	LastSessionSettings->bShouldAdvertise = true;
	LastSessionSettings->bAllowInvites = false;
	LastSessionSettings->bUsesPresence = false;
	LastSessionSettings->bAllowJoinViaPresence = false;
	LastSessionSettings->bUseLobbiesIfAvailable = false;
	// 保留构造函数的 GetBuildUniqueId()；Null 搜索会丢弃版本号不同的房间。
	LastSessionSettings->Set(MultiplayerSessionKeys::MatchType, DesiredMatchType,
	                         EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	LastSessionSettings->Set(MultiplayerSessionKeys::ProjectName, FString(FApp::GetProjectName()),
	                         EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	FString RoomName = FString::Printf(TEXT("BrokenEdge %s"), *DesiredMatchType);
	FParse::Value(FCommandLine::Get(), TEXT("RoomName="), RoomName);
	LastSessionSettings->Set(FName(TEXT("RoomName")), RoomName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	CreateSessionCompleteDelegateHandle = SessionInterface->AddOnCreateSessionCompleteDelegate_Handle(
		CreateSessionCompleteDelegate);
	const bool bSubmitted = SessionInterface->CreateSession(0, NAME_GameSession, *LastSessionSettings);
	// Null 可以同步回调后再返回 false；只在句柄仍有效时补发失败。
	if (!bSubmitted && CreateSessionCompleteDelegateHandle.IsValid()) OnCreateSessionComplete(NAME_GameSession, false);
}

void UMultiplayerSessionsSubsystem::InitializeDedicatedServer()
{
	if (!IsDedicatedServerInstance() || IsBusy() || HasSession()) return;
	int32 Capacity = 8;
	FString ServerMatchType = TEXT("Teams");
	GConfig->GetInt(MultiplayerSessionKeys::DedicatedServerConfig, TEXT("NumPublicConnections"), Capacity, GGameIni);
	GConfig->GetInt(MultiplayerSessionKeys::DedicatedServerConfig, TEXT("MinPlayersToStart"), MinPlayersToStart,
	                GGameIni);
	GConfig->GetString(MultiplayerSessionKeys::DedicatedServerConfig, TEXT("MatchType"), ServerMatchType, GGameIni);
	GConfig->GetBool(MultiplayerSessionKeys::DedicatedServerConfig, TEXT("bAutoStartSession"), bAutoStartSession,
	                 GGameIni);
	// 启动参数覆盖配置，便于一份服务器包运行不同模式和人数。
	FParse::Value(FCommandLine::Get(), TEXT("NumPublicConnections="), Capacity);
	FParse::Value(FCommandLine::Get(), TEXT("MinPlayersToStart="), MinPlayersToStart);
	FParse::Value(FCommandLine::Get(), TEXT("MatchType="), ServerMatchType);
	Capacity = FMath::Clamp(Capacity, 1, 64);
	MinPlayersToStart = FMath::Clamp(MinPlayersToStart, 1, Capacity);
	UE_LOG(LogMultiplayerSessions, Log,
	       TEXT("Dedicated bootstrap: Capacity=%d MinPlayers=%d MatchType=%s AutoStart=%d LocalPlayers=%d"),
	       Capacity, MinPlayersToStart, *ServerMatchType, bAutoStartSession, GetGameInstance()->GetNumLocalPlayers());
	CreateSession(Capacity, ServerMatchType);
}

void UMultiplayerSessionsSubsystem::FindSessions(int32 MaxSearchResults)
{
	if (IsBusy()) return;
	FUniqueNetIdRepl PlayerId;
	if (!RefreshSessionInterface() || !TryGetLocalPlayerId(PlayerId))
	{
		MultiplayerOnFindSessionsComplete.Broadcast(TArray<FOnlineSessionSearchResult>(), false);
		return;
	}
	const IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetWorld());
	LastSessionSearch = MakeShared<FOnlineSessionSearch>();
	LastSessionSearch->MaxSearchResults = FMath::Clamp(MaxSearchResults, 1, 10000);
	LastSessionSearch->bIsLanQuery = Subsystem && Subsystem->GetSubsystemName() == NULL_SUBSYSTEM;
	// 专服不使用 Presence/Lobby；LAN 返回结果后仍需检查 bIsDedicated。
	LastSessionSearch->QuerySettings.Set(SEARCH_DEDICATED_ONLY, true, EOnlineComparisonOp::Equals);
	FindSessionsCompleteDelegateHandle = SessionInterface->AddOnFindSessionsCompleteDelegate_Handle(
		FindSessionsCompleteDelegate);
	if (!SessionInterface->FindSessions(*PlayerId, LastSessionSearch.ToSharedRef()) &&
		FindSessionsCompleteDelegateHandle.IsValid())
	{
		OnFindSessionsComplete(false);
	}
}

void UMultiplayerSessionsSubsystem::JoinSession(const FOnlineSessionSearchResult& SessionResult)
{
	if (IsBusy()) return;
	FUniqueNetIdRepl PlayerId;
	if (!RefreshSessionInterface() || !TryGetLocalPlayerId(PlayerId) || !SessionResult.IsValid()
		|| !SessionResult.Session.SessionSettings.bIsDedicated)
	{
		MultiplayerOnJoinSessionComplete.Broadcast(EOnJoinSessionCompleteResult::UnknownError);
		return;
	}
	if (HasSession())
	{
		MultiplayerOnJoinSessionComplete.Broadcast(EOnJoinSessionCompleteResult::AlreadyInSession);
		return;
	}
	JoinSessionCompleteDelegateHandle = SessionInterface->AddOnJoinSessionCompleteDelegate_Handle(
		JoinSessionCompleteDelegate);
	if (!SessionInterface->JoinSession(*PlayerId, NAME_GameSession, SessionResult) && JoinSessionCompleteDelegateHandle.
		IsValid())
	{
		OnJoinSessionComplete(NAME_GameSession, EOnJoinSessionCompleteResult::UnknownError);
	}
}

void UMultiplayerSessionsSubsystem::DestorySession()
{
	if (IsBusy()) return;
	if (!RefreshSessionInterface())
	{
		OnDestorySessionComplete(NAME_GameSession, false);
		return;
	}
	if (!HasSession())
	{
		OnDestorySessionComplete(NAME_GameSession, true);
		return;
	}
	DestorySessionCompleteDelegateHandle = SessionInterface->AddOnDestroySessionCompleteDelegate_Handle(
		DestorySessionCompleteDelegate);
	if (!SessionInterface->DestroySession(NAME_GameSession) && DestorySessionCompleteDelegateHandle.IsValid())
	{
		OnDestorySessionComplete(NAME_GameSession, false);
	}
}

void UMultiplayerSessionsSubsystem::StartSession()
{
	if (IsBusy()) return;
	if (!RefreshSessionInterface() || !HasSession())
	{
		MultiplayerOnStartSessionComplete.Broadcast(false);
		return;
	}
	if (SessionInterface->GetSessionState(NAME_GameSession) == EOnlineSessionState::InProgress)
	{
		MultiplayerOnStartSessionComplete.Broadcast(true);
		return;
	}
	StartSessionCompleteDelegateHandle = SessionInterface->AddOnStartSessionCompleteDelegate_Handle(
		StartSessionCompleteDelegate);
	if (!SessionInterface->StartSession(NAME_GameSession) && StartSessionCompleteDelegateHandle.IsValid())
	{
		OnStartSessionComplete(NAME_GameSession, false);
	}
}

void UMultiplayerSessionsSubsystem::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (SessionName != NAME_GameSession) return;
	if (SessionInterface) SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(
		CreateSessionCompleteDelegateHandle);
	CreateSessionCompleteDelegateHandle.Reset();
	UE_LOG(LogMultiplayerSessions, Log, TEXT("Create complete: Success=%d Players=%d MatchType=%s"), bWasSuccessful,
	       DesiredNumPublicConnections, *DesiredMatchType);
	MultiplayerOnCreateSessionComplete.Broadcast(bWasSuccessful);
}

void UMultiplayerSessionsSubsystem::EndSession()
{
	if (IsBusy()) return;
	if (!RefreshSessionInterface() || !HasSession())
	{
		MultiplayerOnEndSessionComplete.Broadcast(false);
		return;
	}
	const auto State = SessionInterface->GetSessionState(NAME_GameSession);
	if (State == EOnlineSessionState::Ended || State == EOnlineSessionState::Pending)
	{
		MultiplayerOnEndSessionComplete.Broadcast(true);
		return;
	}
	EndSessionCompleteDelegateHandle = SessionInterface->AddOnEndSessionCompleteDelegate_Handle(
		FOnEndSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnEndSessionComplete));
	if (!SessionInterface->EndSession(NAME_GameSession) && EndSessionCompleteDelegateHandle.IsValid())
		OnEndSessionComplete(NAME_GameSession, false);
}

void UMultiplayerSessionsSubsystem::OnEndSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (SessionName != NAME_GameSession) return;
	if (SessionInterface) SessionInterface->ClearOnEndSessionCompleteDelegate_Handle(EndSessionCompleteDelegateHandle);
	EndSessionCompleteDelegateHandle.Reset();
	MultiplayerOnEndSessionComplete.Broadcast(bWasSuccessful);
}

void UMultiplayerSessionsSubsystem::OnFindSessionsComplete(bool bWasSuccessful)
{
	if (SessionInterface) SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(
		FindSessionsCompleteDelegateHandle);
	FindSessionsCompleteDelegateHandle.Reset();
	// 快照防止监听者重新搜索时使其他监听者收到失效引用。
	const TArray<FOnlineSessionSearchResult> Results = bWasSuccessful && LastSessionSearch.IsValid()
		                                                   ? LastSessionSearch->SearchResults
		                                                   : TArray<FOnlineSessionSearchResult>();
	UE_LOG(LogMultiplayerSessions, Log, TEXT("Find complete: Success=%d Results=%d"), bWasSuccessful, Results.Num());
	MultiplayerOnFindSessionsComplete.Broadcast(Results, bWasSuccessful && LastSessionSearch.IsValid());
}

void UMultiplayerSessionsSubsystem::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	if (SessionName != NAME_GameSession) return;
	if (SessionInterface) SessionInterface->
		ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
	JoinSessionCompleteDelegateHandle.Reset();
	UE_LOG(LogMultiplayerSessions, Log, TEXT("Join complete: Result=%d"), static_cast<int32>(Result));
	MultiplayerOnJoinSessionComplete.Broadcast(Result);
}

void UMultiplayerSessionsSubsystem::OnDestorySessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (SessionName != NAME_GameSession) return;
	if (SessionInterface) SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(
		DestorySessionCompleteDelegateHandle);
	DestorySessionCompleteDelegateHandle.Reset();
	const bool bRecreate = bCreateSessionOnDestory;
	bCreateSessionOnDestory = false;
	UE_LOG(LogMultiplayerSessions, Log, TEXT("Destroy complete: Success=%d Recreate=%d"), bWasSuccessful, bRecreate);
	if (bRecreate)
	{
		if (bWasSuccessful) CreateSession(LastNumPublicConnections, LastMatchType);
		else MultiplayerOnCreateSessionComplete.Broadcast(false);
	}
	MultiplayerOnDestorySessionComplete.Broadcast(bWasSuccessful);
}

void UMultiplayerSessionsSubsystem::OnStartSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (SessionName != NAME_GameSession) return;
	if (SessionInterface) SessionInterface->ClearOnStartSessionCompleteDelegate_Handle(
		StartSessionCompleteDelegateHandle);
	StartSessionCompleteDelegateHandle.Reset();
	UE_LOG(LogMultiplayerSessions, Log, TEXT("Start complete: Result=%d"), bWasSuccessful);
	MultiplayerOnStartSessionComplete.Broadcast(bWasSuccessful);
}
