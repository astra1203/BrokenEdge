// Fill out your copyright notice in the Description page of Project Settings.


#include "MultiplayerSessions/public/MultiplayerSessionsSubsystem.h"

#include "Interfaces/OnlineSessionInterface.h"
#include "MultiplayerSessions/public/MultiplayerSessions.h"
// #include "OnlineSubsystem.h"
// #include "OnlineSubsystemNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"


namespace MultiplayerSessionsConstants
{
	// OnlineSubsystem 的 int32 重载以本地用户编号作为调用者。
	// Dedicated Server 没有 ULocalPlayer，Null 实现允许使用 0 号用户创建托管会话。
	constexpr int32 LocalUserNumber = 0;

	// 对外参数即使来自 C++ 也要做上限保护，避免配置错误创建异常大的会话/搜索请求。
	constexpr int32 MaxSupportedPublicConnections = 64;
	constexpr int32 MaxSupportedSearchResults = 100;
	
}

void UMultiplayerSessionsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	// 不在 UObject 构造函数中获取 OSS：构造函数也会为 CDO 执行，且那时没有可靠的 World。
	// Initialize 已进入具体 GameInstance 生命周期，能够拿到 PIE/客户端/服务器各自的 OSS 实例。
	RefreshSessionInterface();
	
	// CreateSession();
	CreateSession(8,TEXT("FreeForAll"));
}

bool UMultiplayerSessionsSubsystem::IsDedicatedServerInstance() const
{
	// 以 World 的 NetMode 为最终依据。仅仅编译了 Server Target，并不代表任意时刻都已有可用的服务器 World。
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() == NM_DedicatedServer;
}

void UMultiplayerSessionsSubsystem::BroadcastCreateSessionComplete(const bool bWasSuccessful)
{
	// 先通知 Blueprint，再通知原生 C++。自动启动器位于原生委托中，
	// 因而蓝图一定先看到 Create 完成，之后才可能收到自动 Start 的完成事件。
	// OnCreateSessionComplete.Broadcast(bWasSuccessful);
	// OnCreateSessionCompleteNative.Broadcast(bWasSuccessful);
	
}

bool UMultiplayerSessionsSubsystem::RefreshSessionInterface()
{
	// 使用 Online::GetSessionInterface(GetWorld())，而不是 IOnlineSubsystem::Get()。
	// 前者会考虑 PIE World Context，多个 PIE 实例才不会错误共享同一个 SessionInterface。
	const IOnlineSessionPtr WorldSessionInterface = Online::GetSessionInterface(GetWorld());
	if (WorldSessionInterface != SessionInterface)
	{
		// World/OSS 实例发生变化时，旧接口上的委托句柄不能拿到新接口上清理。
		// 所以必须在替换共享指针之前先解除旧绑定。
		// ClearOnlineDelegates();
		SessionInterface = WorldSessionInterface;
	}

	return SessionInterface.IsValid();
}

// bool UMultiplayerSessionsSubsystem::ValidateNullSubsystem(const TCHAR* OperationName) const
// {
// 	// 本插件当前按用户选择明确锁定 OnlineSubsystemNull。
// 	// 显式校验能让 DefaultPlatformService 拼写或插件启用错误尽早暴露在日志和失败事件中。
// 	const IOnlineSubsystem* OnlineSubsystem = Online::GetSubsystem(GetWorld());
// 	if (OnlineSubsystem && OnlineSubsystem->GetSubsystemName() == NULL_SUBSYSTEM)
// 	{
// 		return true;
// 	}
//
// 	UE_LOG(
// 		LogMultiplayerSessions,
// 		Error,
// 		TEXT("%s failed: OnlineSubsystemNull is not active (active subsystem: %s)."),
// 		OperationName,
// 		OnlineSubsystem ? *OnlineSubsystem->GetSubsystemName().ToString() : TEXT("None"));
// 	return false;
// }

void UMultiplayerSessionsSubsystem::CreateSession(int32 NumPublicConnections, FString MatchType)
{
	DesiredNumPublicConnections = FMath::Max(1, NumPublicConnections);
	DesiredMatchType = MoveTemp(MatchType);//std::move()

	if (!RefreshSessionInterface())
	{
		UE_LOG(LogTemp, Error, TEXT("无法创建会话：在线会话接口不可用。"));
		MultiplayerOnCreateSessionComplete.Broadcast(false);
		return;
	}

	// 已存在同名会话则先销毁，销毁完成后再重建。
	if (SessionInterface->GetNamedSession(NAME_GameSession))
	{
		bCreateSessionOnDestory = true;
		LastNumPublicConnections = DesiredNumPublicConnections;
		LastMatchType = DesiredMatchType;
		//待办事项
		// @ TODO
		// DestorySession();
		return;
	}

	IOnlineSubsystem* OnlineSubsystem = Online::GetSubsystem(GetWorld());

	const bool bIsDedicated = IsDedicatedServerInstance();
	// const bool bExplicitNullTest = FParse::Param(FCommandLine::Get(), TEXT("NOSTEAM"));

	// if (bIsDedicated &&
	// 	!GIsEditor &&
	// 	!bExplicitNullTest &&
	// 	(!OnlineSubsystem || OnlineSubsystem->GetSubsystemName() != STEAM_SUBSYSTEM))
	// {
	// 	UE_LOG(
	// 		LogTemp,
	// 		Error,
	// 		TEXT("Dedicated server requires Steam OSS, but the active subsystem is '%s'. "
	// 			"Check Steam initialization, AppId and ports; use -NOSTEAM only for an explicit LAN test."),
	// 		OnlineSubsystem ? *OnlineSubsystem->GetSubsystemName().ToString() : TEXT("None"));
	//
	// 	MultiplayerOnCreateSessionComplete.Broadcast(false);
	// 	return;
	// }
	
	//@ TODO
	if (!bIsDedicated)
	{
		UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
		       TEXT("不是专用服务器"));
		return;
	}

	CreateSessionCompleteDelegateHandle =
		SessionInterface->AddOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegate);

	LastSessionSettings = MakeShared<FOnlineSessionSettings>();

	LastSessionSettings->bIsLANMatch = OnlineSubsystem && OnlineSubsystem->GetSubsystemName() == NULL_SUBSYSTEM;
	LastSessionSettings->bIsDedicated = bIsDedicated;
	LastSessionSettings->NumPublicConnections = DesiredNumPublicConnections;
	LastSessionSettings->bAllowJoinInProgress = true;
	LastSessionSettings->bShouldAdvertise = true;
	// 专用服务器不走 Presence / 邀请 / 大厅那一套。
	LastSessionSettings->bAllowInvites = !bIsDedicated;
	LastSessionSettings->bAllowJoinViaPresence = !bIsDedicated;
	LastSessionSettings->bUsesPresence = !bIsDedicated;
	LastSessionSettings->bUseLobbiesIfAvailable = !bIsDedicated;

	// LastSessionSettings->Set(
	// 	BlasterSessionKeys::MatchType,
	// 	DesiredMatchType,
	// 	EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	// // 用项目名过滤，避免搜到其它项目的会话。
	// LastSessionSettings->Set(
	// 	BlasterSessionKeys::ProjectName,
	// 	BlasterSessionKeys::ProjectValue,
	// 	EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	// LastSessionSettings->Set(
	// 	SETTING_GAMEMODE,
	// 	DesiredMatchType,
	// 	EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// if (const UWorld* World = GetWorld())
	// {
	// 	LastSessionSettings->Set(
	// 		SETTING_MAPNAME,
	// 		World->GetMapName(),
	// 		EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	// }

	bool bRequestStarted = false;
	if (bIsDedicated)
	{
		// 专用服务器没有客户端 Steam 身份，用 0 号本地玩家创建会话。
		bRequestStarted = SessionInterface->CreateSession(0, NAME_GameSession, *LastSessionSettings);
	}

	if (!bRequestStarted)
	{
		SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
		CreateSessionCompleteDelegateHandle.Reset();
		MultiplayerOnCreateSessionComplete.Broadcast(false);
		UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
		       TEXT("创建会话失败"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
		       TEXT("创建会话成功"));
	}
}

void UMultiplayerSessionsSubsystem::FindSessions(const int32 MaxSearchResults)
{
	if (!RefreshSessionInterface())
	{
		MultiplayerOnFindSessionsComplete.Broadcast(TArray<FOnlineSessionSearchResult>(), false);
		return;
	}

	FindSessionsCompleteDelegateHandle =
		SessionInterface->AddOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegate);

	LastSessionSearch = MakeShared<FOnlineSessionSearch>();
	LastSessionSearch->MaxSearchResults = FMath::Clamp(MaxSearchResults, 1, 100);

	IOnlineSubsystem* OnlineSubsystem = Online::GetSubsystem(GetWorld());

	const bool bExplicitLanSearch = FParse::Param(FCommandLine::Get(), TEXT("LANSEARCH"));
	
	LastSessionSearch->bIsLanQuery = bExplicitLanSearch ||
		(OnlineSubsystem && OnlineSubsystem->GetSubsystemName() == NULL_SUBSYSTEM);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("Searching sessions: OSS=%s LAN=%s MaxResults=%d"),
		OnlineSubsystem ? *OnlineSubsystem->GetSubsystemName().ToString() : TEXT("None"),
		LastSessionSearch->bIsLanQuery ? TEXT("true") : TEXT("false"),
		LastSessionSearch->MaxSearchResults);

	// Presence 搜索选的是 Steam 大厅；专用服务器通过 Steam Game Server API 广播，
	// 因此显式只搜索专用服务器。
	// if (OnlineSubsystem && OnlineSubsystem->GetSubsystemName() == STEAM_SUBSYSTEM)
	// {
	// 	LastSessionSearch->QuerySettings.Set(
	// 		SEARCH_DEDICATED_ONLY,
	// 		true,
	// 		EOnlineComparisonOp::Equals);
	// }

	bool bRequestStarted = false;
	if (const UWorld* World = GetWorld())
	{
		const ULocalPlayer* LocalPlayer = World->GetFirstLocalPlayerFromController();
		if (LocalPlayer && LocalPlayer->GetPreferredUniqueNetId().IsValid())
		{
			bRequestStarted = SessionInterface->FindSessions(
				*LocalPlayer->GetPreferredUniqueNetId(),
				LastSessionSearch.ToSharedRef());
		}
	}

	if (!bRequestStarted)
	{
		SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
		FindSessionsCompleteDelegateHandle.Reset();
		MultiplayerOnFindSessionsComplete.Broadcast(TArray<FOnlineSessionSearchResult>(), false);
		UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
		       TEXT("搜索会话失败"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
		       TEXT("搜索会话成功"));
	}
}

// void UMultiplayerSessionsSubsystem::JoinSession(const FMultiplayerSessionSearchResult& SearchResult)
// {
// 	// 蓝图结构只是缓存索引。既校验搜索代数，又校验当前数组下标，
// 	// 才能保证取回的原生对象就是 UI 当时展示的那一项。
// 	if (!LastSessionSearch.IsValid() ||
// 		!LastSessionSearch->SearchResults.IsValidIndex(SearchResult.ResultIndex))
// 	{
// 		UE_LOG(LogMultiplayerSessions, Warning, TEXT("JoinSession rejected a stale or invalid Blueprint search result."));
// 		// BroadcastJoinSessionComplete(EOnJoinSessionCompleteResult::SessionDoesNotExist);
// 		return;
// 	}
//
// 	// 通过验证后交给原生入口，后续 C++ 和 Blueprint 共用完全相同的 Join 流程。
// 	// JoinSessionNative(LastSessionSearch->SearchResults[SearchResult.ResultIndex]);
//
//
// 	// Dedicated Server 只托管会话；加入其他会话是客户端行为。
// 	if (IsDedicatedServerInstance())
// 	{
// 		UE_LOG(LogMultiplayerSessions, Warning, TEXT("JoinSession is a client operation and is unavailable on Dedicated Server."));
// 		BroadcastJoinSessionComplete(EOnJoinSessionCompleteResult::UnknownError);
// 		return;
// 	}
//
// 	// IsValid 会同时检查拥有者 ID 和平台 SessionInfo，避免把空快照交给 OSS。
// 	if (!SearchResult.IsValid() || !RefreshSessionInterface() || !ValidateNullSubsystem(TEXT("JoinSession")))
// 	{
// 		BroadcastJoinSessionComplete(EOnJoinSessionCompleteResult::UnknownError);
// 		return;
// 	}
//
// 	// 一个客户端同时只能维护一个明确的加入流程，防止多个结果争用 NAME_GameSession。
// 	if (JoinSessionCompleteDelegateHandle.IsValid())
// 	{
// 		UE_LOG(LogMultiplayerSessions, Warning, TEXT("JoinSession ignored because another join request is active."));
// 		BroadcastJoinSessionComplete(EOnJoinSessionCompleteResult::UnknownError);
// 		return;
// 	}
//
// 	// 完成委托必须先注册；成功回调中还会通过 NAME_GameSession 解析 ConnectString。
// 	const FOnJoinSessionCompleteDelegate CompletionDelegate =
// 		FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleJoinSessionComplete);
// 	JoinSessionCompleteDelegateHandle =
// 		SessionInterface->AddOnJoinSessionCompleteDelegate_Handle(CompletionDelegate);
//
// 	// 子系统只请求加入会话，不执行 ClientTravel。上层在成功事件中拿 ConnectString 决定跳转时机。
// 	if (!SessionInterface->JoinSession(
// 		MultiplayerSessionsConstants::LocalUserNumber,
// 		NAME_GameSession,
// 		SearchResult))
// 	{
// 		// 请求未进入异步流程，清除句柄并立刻通知所有监听者。
// 		SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
// 		JoinSessionCompleteDelegateHandle.Reset();
// 		BroadcastJoinSessionComplete(EOnJoinSessionCompleteResult::UnknownError);
// 	}
//
// }

// void UMultiplayerSessionsSubsystem::JoinSessionNative(const FOnlineSessionSearchResult& SearchResult)
// {
// }

// void UMultiplayerSessionsSubsystem::JoinSession(const FOnlineSessionSearchResult& SessionResult)
// {
// 	if (!RefreshSessionInterface())
// 	{
// 		OnJoinSessionComplete.Broadcast(EOnJoinSessionCompleteResult::UnknownError);
// 		return;
// 	}
// }

// void UMultiplayerSessionsSubsystem::HandleFindSessionsComplete(const bool bWasSuccessful)
// {
// 	// 搜索对象仍要保留，因为 SearchResults 既要转换给蓝图，也可能被 C++ 调用方继续读取。
// 	if (SessionInterface.IsValid())
// 	{
// 		SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
// 	}
// 	FindSessionsCompleteDelegateHandle.Reset();
//
// 	UE_LOG(
// 		LogMultiplayerSessions,
// 		Log,
// 		TEXT("LAN session search completed: success=%s, results=%d."),
// 		bWasSuccessful ? TEXT("true") : TEXT("false"),
// 		LastSessionSearch.IsValid() ? LastSessionSearch->SearchResults.Num() : 0);
// 	BroadcastFindSessionsComplete(bWasSuccessful);
// }

// EMultiplayerJoinSessionResult UMultiplayerSessionsSubsystem::ToBlueprintJoinResult(
// 	const EOnJoinSessionCompleteResult::Type Result)
// {
// 	// EOnJoinSessionCompleteResult::Type 是旧式 namespace enum，不能直接作为 Blueprint 参数；
// 	// 在边界处逐项转换，避免向蓝图泄露 OnlineSubsystem 的非反射类型。
// 	switch (Result)
// 	{
// 	case EOnJoinSessionCompleteResult::Success:
// 		return EMultiplayerJoinSessionResult::Success;
// 	case EOnJoinSessionCompleteResult::SessionIsFull:
// 		return EMultiplayerJoinSessionResult::SessionIsFull;
// 	case EOnJoinSessionCompleteResult::SessionDoesNotExist:
// 		return EMultiplayerJoinSessionResult::SessionDoesNotExist;
// 	case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress:
// 		return EMultiplayerJoinSessionResult::CouldNotRetrieveAddress;
// 	case EOnJoinSessionCompleteResult::AlreadyInSession:
// 		return EMultiplayerJoinSessionResult::AlreadyInSession;
// 	case EOnJoinSessionCompleteResult::UnknownError:
// 	default:
// 		return EMultiplayerJoinSessionResult::UnknownError;
// 	}
// }
