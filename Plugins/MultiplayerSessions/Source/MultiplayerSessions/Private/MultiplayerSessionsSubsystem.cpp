// Fill out your copyright notice in the Description page of Project Settings.


#include "MultiplayerSessions/public/MultiplayerSessionsSubsystem.h"

#include "Interfaces/OnlineSessionInterface.h"
#include "MultiplayerSessions/public/MultiplayerSessions.h"
// #include "OnlineSubsystem.h"
// #include "OnlineSubsystemNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"
#include "Online/OnlineSessionNames.h"


namespace MultiplayerSessionsConstants
{
	// OnlineSubsystem 的 int32 重载以本地用户编号作为调用者。
	// Dedicated Server 没有 ULocalPlayer，Null 实现允许使用 0 号用户创建托管会话。
	constexpr int32 LocalUserNumber = 0;

	// 对外参数即使来自 C++ 也要做上限保护，避免配置错误创建异常大的会话/搜索请求。
	constexpr int32 MaxSupportedPublicConnections = 64;
	constexpr int32 MaxSupportedSearchResults = 100;
	
}

// void UMultiplayerSessionsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
// {
// 	Super::Initialize(Collection);
//
// 	// 不在 UObject 构造函数中获取 OSS：构造函数也会为 CDO 执行，且那时没有可靠的 World。
// 	// Initialize 已进入具体 GameInstance 生命周期，能够拿到 PIE/客户端/服务器各自的 OSS 实例。
// 	RefreshSessionInterface();
//
// 	// CreateSession();
// 	//
// 	CreateSession(8,TEXT("FreeForAll"));
// }


UMultiplayerSessionsSubsystem::UMultiplayerSessionsSubsystem():
    CreateSessionCompleteDelegate(FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnCreateSessionComplete)),
	FindSessionsCompleteDelegate(FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::OnFindSessionsComplete))
	// JoinSessionCompleteDelegate(FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnJoinSessionComplete)),
	// DestorySessionCompleteDelegate(FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::OnDestorySessionComplete)),
{
	// CreateSessionCompleteDelegate = FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnCreateSessionComplete);
}

void UMultiplayerSessionsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	// GameInstance Subsystem 实例进入初始化阶段后，再取得项目当前使用的在线平台。
	// 编辑器里常见的是 NULL 子系统，打包并正确配置平台服务后也可能是 Steam 等子系统。
	IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
	if (Subsystem)
	{
		// bIsLANMatch = Subsystem->GetSubsystemName() == FName(TEXT("NULL"));
		// SessionInterface 是统一的“联机会话办事窗口”，后面的建房、搜房等请求都交给它。
		SessionInterface = Subsystem->GetSessionInterface();
	}
	// 如果没有可用的 Online Subsystem，SessionInterface 会保持无效；各操作函数会在使用前检查它。
}

bool UMultiplayerSessionsSubsystem::IsDedicatedServerInstance() const
{
	// 以 World 的 NetMode 为最终依据。仅仅编译了 Server Target，并不代表任意时刻都已有可用的服务器 World。
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() == NM_DedicatedServer;
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
		// return;
	}

	CreateSessionCompleteDelegateHandle =
		SessionInterface->AddOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegate);


	// 配置本次会话的设置：是否局域网、最大连接数、是否可加入、是否广播等。
	LastSessionSettings = MakeShareable(new FOnlineSessionSettings());
	LastSessionSettings->bIsLANMatch = IOnlineSubsystem::Get()->GetSubsystemName() == "NULL" ? true : false;
	LastSessionSettings->NumPublicConnections = NumPublicConnections;
	LastSessionSettings->bAllowJoinInProgress = true;
	LastSessionSettings->bAllowJoinViaPresence = true;
	LastSessionSettings->bShouldAdvertise = true;
	LastSessionSettings->bUsesPresence = true;
	LastSessionSettings->bUseLobbiesIfAvailable = true;
	// 把比赛类型（MatchType）写入会话设置，供查找时匹配过滤。
	LastSessionSettings->Set(FName("MatchType"), MatchType, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	LastSessionSettings->BuildUniqueId = 1;

	const ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();
	if (!SessionInterface->CreateSession(*LocalPlayer->GetPreferredUniqueNetId(), NAME_GameSession, *LastSessionSettings))
	{
		// 创建失败：移除委托并广播失败结果。
		UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
		       TEXT("创建会话失败"));
		SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);

		MultiplayerOnCreateSessionComplete.Broadcast(false);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
		       TEXT("创建会话成功"));
	}

	// LastSessionSettings = MakeShared<FOnlineSessionSettings>();
	//
	// LastSessionSettings->bIsLANMatch = OnlineSubsystem && OnlineSubsystem->GetSubsystemName() == NULL_SUBSYSTEM;
	// LastSessionSettings->bIsDedicated = bIsDedicated;
	// LastSessionSettings->NumPublicConnections = DesiredNumPublicConnections;
	// LastSessionSettings->bAllowJoinInProgress = true;
	// LastSessionSettings->bShouldAdvertise = true;
	// // 专用服务器不走 Presence / 邀请 / 大厅那一套。
	// LastSessionSettings->bAllowInvites = !bIsDedicated;
	// LastSessionSettings->bAllowJoinViaPresence = !bIsDedicated;
	// LastSessionSettings->bUsesPresence = !bIsDedicated;
	// LastSessionSettings->bUseLobbiesIfAvailable = !bIsDedicated;

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

	// bool bRequestStarted = false;
	// if (bIsDedicated)
	// {
	// 	// 专用服务器没有客户端 Steam 身份，用 0 号本地玩家创建会话。
	// 	bRequestStarted = SessionInterface->CreateSession(0, NAME_GameSession, *LastSessionSettings);
	// }
	//
	// if (!bRequestStarted)
	// {
	// 	SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
	// 	CreateSessionCompleteDelegateHandle.Reset();
	// 	MultiplayerOnCreateSessionComplete.Broadcast(false);
	// 	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
	// 	       TEXT("创建会话失败"));
	// }
	// else
	// {
	// 	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
	// 	       TEXT("创建会话成功"));
	// }
}

void UMultiplayerSessionsSubsystem::FindSessions(const int32 MaxSearchResults)
{
	// 接口无效时直接返回，避免后续空指针访问。
	if (!SessionInterface.IsValid())
	{
		return;
	}

	FindSessionsCompleteDelegateHandle = SessionInterface->AddOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegate);

	// 配置查找参数：最大结果数、是否局域网查询，以及基于 Presence 的查询条件。
	LastSessionSearch = MakeShareable(new FOnlineSessionSearch());
	LastSessionSearch->MaxSearchResults = MaxSearchResults;
	LastSessionSearch->bIsLanQuery = IOnlineSubsystem::Get()->GetSubsystemName() == "NULL" ? true : false;
	LastSessionSearch->QuerySettings.Set(SEARCH_PRESENCE, true, EOnlineComparisonOp::Equals);

	//我断定某个变量这个时候一定是某值，否则就报错，ensure()，assert(变量 != nullptr)
	const ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();
	if (!SessionInterface->FindSessions(*LocalPlayer->GetPreferredUniqueNetId(), LastSessionSearch.ToSharedRef()))
	{
		SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);

		MultiplayerOnFindSessionsComplete.Broadcast(TArray<FOnlineSessionSearchResult>(), false);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT("查找会话成功"));
	
	
}

// 加入指定的会话（传入的搜索结果）。加入结果通过委托广播，失败时使用 UnknownError。
void UMultiplayerSessionsSubsystem::JoinSession(const FOnlineSessionSearchResult& SessionResult)
{
	// 接口无效时直接广播错误，让调用方知道加入失败。
	if (!SessionInterface.IsValid())
	{
		MultiplayerOnJoinSessionComplete.Broadcast(EOnJoinSessionCompleteResult::UnknownError);
		return;
	}

	JoinSessionCompleteDelegateHandle = SessionInterface->AddOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegate);

	const ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();
	if (!SessionInterface->JoinSession(*LocalPlayer->GetPreferredUniqueNetId(), NAME_GameSession, SessionResult))
	{
		SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);

		MultiplayerOnJoinSessionComplete.Broadcast(EOnJoinSessionCompleteResult::UnknownError);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT("加入会话成功"));
}

// 销毁指定会话。销毁结果通过委托广播；若此前标记了“销毁后重建”，会由回调负责重建。
void UMultiplayerSessionsSubsystem::DestorySession()
{
	// 接口无效时直接广播失败。
	if (!SessionInterface.IsValid())
	{
		MultiplayerOnDestorySessionComplete.Broadcast(false);
		return;
	}

	DestorySessionCompleteDelegateHandle =  SessionInterface->AddOnDestroySessionCompleteDelegate_Handle(DestorySessionCompleteDelegate);

	if (!SessionInterface->DestroySession(NAME_GameSession))
	{
		SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestorySessionCompleteDelegateHandle);
		MultiplayerOnDestorySessionComplete.Broadcast(false);
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT("销毁会话成功"));
}

void UMultiplayerSessionsSubsystem::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	// 会话创建完成：先移除委托句柄，再向外广播结果。
	if (SessionInterface)
	{
		SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
	}

	MultiplayerOnCreateSessionComplete.Broadcast(bWasSuccessful);
}

void UMultiplayerSessionsSubsystem::OnFindSessionsComplete(bool bWasSuccessful)
{

	// 搜索已经结束，无论成功还是失败都先移除这一次注册的回调。
	if (SessionInterface)
	{
		SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
	}
	FindSessionsCompleteDelegateHandle.Reset();

	const int32 SearchResultCount = LastSessionSearch.IsValid() ? LastSessionSearch->SearchResults.Num() : 0;
	UE_LOG(
		LogTemp,
		Log,
		TEXT("Oreki [%llu][%s] Line:%d | FindSessions: Completed. Success: %s, Results: %d"),
		GFrameCounter,
		TEXT(__FUNCTION__),
		__LINE__,
		bWasSuccessful ? TEXT("true") : TEXT("false"),
		SearchResultCount
	);

	if (!bWasSuccessful || !LastSessionSearch.IsValid())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Oreki [%llu][%s] Line:%d | %s"),
			GFrameCounter,
			TEXT(__FUNCTION__),
			__LINE__,
			TEXT("FindSessions: Search failed or the search object is invalid.")
		);
		MultiplayerOnFindSessionsComplete.Broadcast(TArray<FOnlineSessionSearchResult>(), false);
		return;
	}

	for (int32 ResultIndex = 0; ResultIndex < LastSessionSearch->SearchResults.Num(); ++ResultIndex)
	{
		const FOnlineSessionSearchResult& Result = LastSessionSearch->SearchResults[ResultIndex];
		FString FoundMatchType = TEXT("<NotSet>");
		Result.Session.SessionSettings.Get(FName(TEXT("MatchType")), FoundMatchType);

		UE_LOG(
			LogTemp,
			Log,
			TEXT("Oreki [%llu][%s] Line:%d | FindSessions: Result[%d] SessionId=%s, Owner=%s, MatchType=%s, Ping=%dms, OpenPublic=%d/%d, LAN=%s, Presence=%s"),
			GFrameCounter,
			TEXT(__FUNCTION__),
			__LINE__,
			ResultIndex,
			*Result.GetSessionIdStr(),
			*Result.Session.OwningUserName,
			*FoundMatchType,
			Result.PingInMs,
			Result.Session.NumOpenPublicConnections,
			Result.Session.SessionSettings.NumPublicConnections,
			Result.Session.SessionSettings.bIsLANMatch ? TEXT("true") : TEXT("false"),
			Result.Session.SessionSettings.bUsesPresence ? TEXT("true") : TEXT("false")
		);
	}

	// 搜索成功时原样返回结果；空数组代表“搜索成功，但没有可见会话”。
	// Menu 会逐条读取每个结果中的 MatchType，自行挑选匹配的房间并调用 JoinSession。
	MultiplayerOnFindSessionsComplete.Broadcast(LastSessionSearch->SearchResults, true);
}
