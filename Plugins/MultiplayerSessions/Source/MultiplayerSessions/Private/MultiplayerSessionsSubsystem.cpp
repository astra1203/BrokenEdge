// Copyright Epic Games, Inc. All Rights Reserved.

#include "MultiplayerSessionsSubsystem.h"

#include "Engine/World.h"
#include "MultiplayerSessions.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemUtils.h"

namespace MultiplayerSessionsConstants
{
	// OnlineSubsystem 的 int32 重载要求一个本地用户编号。
	// Dedicated Server 没有 ULocalPlayer；OnlineSubsystemNull 接受 0 作为调用用户。
	constexpr int32 LocalUserNumber = 0;

	// 对外参数即使来自 C++ 也可能越界，因此在进入 OSS 前设置统一安全上限。
	constexpr int32 MaxSupportedPublicConnections = 64;
	constexpr int32 MaxSupportedSearchResults = 1000;

	// 与 Config/DefaultGame.ini 中的 [MultiplayerSessions.DedicatedServer] 对应。
	constexpr TCHAR DedicatedServerConfigSection[] = TEXT("MultiplayerSessions.DedicatedServer");

	// 会话广告中的地图字段。搜索结果可以读取它并显示服务器当前地图。
	const FName MapNameSettingKey(TEXT("MAPNAME"));
}

void UMultiplayerSessionsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 这五个成员是交给 OnlineSubsystem 的“单播回调对象”。
	// CreateUObject 对 this 使用弱 UObject 引用；对象失效后不会调用悬空地址。
	// 委托本体只需初始化一次，每次异步请求只需要重新注册并保存新的 Handle。
	CreateSessionCompleteDelegate = FOnCreateSessionCompleteDelegate::CreateUObject(
		this,
		&ThisClass::HandleCreateSessionComplete);
	FindSessionsCompleteDelegate = FOnFindSessionsCompleteDelegate::CreateUObject(
		this,
		&ThisClass::HandleFindSessionsComplete);
	JoinSessionCompleteDelegate = FOnJoinSessionCompleteDelegate::CreateUObject(
		this,
		&ThisClass::HandleJoinSessionComplete);
	DestroySessionCompleteDelegate = FOnDestroySessionCompleteDelegate::CreateUObject(
		this,
		&ThisClass::HandleDestroySessionComplete);
	StartSessionCompleteDelegate = FOnStartSessionCompleteDelegate::CreateUObject(
		this,
		&ThisClass::HandleStartSessionComplete);

	// 必须按当前 World Context 获取接口。PIE 的多个 World 不能可靠地共用全局默认 OSS 实例。
	RefreshSessionInterface();

	// 普通客户端和 Listen Server 不在初始化阶段自动建会话。
	// 它们可以之后由菜单显式调用 CreateSession，也可以使用 IP 地址直接连接。
	if (!IsDedicatedServerInstance())
	{
		return;
	}

	// Dedicated Server 没有主机菜单，因此从配置读取容量和自动启动策略。
	// 配置不存在时保留代码中的默认值：8 个公开连接并自动启动。
	int32 NumPublicConnections = 8;
	GConfig->GetInt(
		MultiplayerSessionsConstants::DedicatedServerConfigSection,
		TEXT("NumPublicConnections"),
		NumPublicConnections,
		GGameIni);
	GConfig->GetBool(
		MultiplayerSessionsConstants::DedicatedServerConfigSection,
		TEXT("bAutoStartSession"),
		bAutoStartDedicatedSession,
		GGameIni);

	// 这里的“自动启动”包括先创建会话；创建异步成功后，完成回调再调用 StartSession。
	if (bAutoStartDedicatedSession)
	{
		CreateSession(NumPublicConnections);
	}
}

void UMultiplayerSessionsSubsystem::Deinitialize()
{
	// 先从 OSS 的多播列表中删除本对象的全部注册，再释放异步参数和接口。
	// 这既避免退出阶段收到回调，也避免长期 OSS 实例积累无效注册。
	ClearOnlineDelegates();
	LastSessionSearch.Reset();
	LastSessionSettings.Reset();
	SessionInterface.Reset();

	Super::Deinitialize();
}

void UMultiplayerSessionsSubsystem::CreateSession(const int32 NumPublicConnections)
{
	// Blueprint 的 ClampMin 只约束蓝图节点输入；C++ 仍能传入任意值，因此这里再次限制范围。
	// Pending 值还会跨越“销毁旧会话 -> 创建新会话”的异步链路。
	PendingNumPublicConnections = FMath::Clamp(
		NumPublicConnections,
		1,
		MultiplayerSessionsConstants::MaxSupportedPublicConnections);

	// 每次操作前刷新接口，兼容 PIE World 切换和 OnlineSubsystem 实例变化。
	if (!RefreshSessionInterface())
	{
		UE_LOG(LogMultiplayerSessions, Error, TEXT("CreateSession failed: no Online Session interface."));
		MultiplayerOnCreateSessionComplete.Broadcast(false);
		return;
	}

	// 有效 Handle 表示上一次创建请求尚未进入完成回调。
	// 同类请求不允许并发，否则单个成员设置和完成句柄会互相覆盖。
	if (CreateSessionCompleteDelegateHandle.IsValid())
	{
		UE_LOG(LogMultiplayerSessions, Warning, TEXT("CreateSession ignored: another create request is active."));
		MultiplayerOnCreateSessionComplete.Broadcast(false);
		return;
	}

	// OSS 不允许同名 NAME_GameSession 被重复创建。
	// 记录“销毁后重建”意图，销毁完成回调会使用 PendingNumPublicConnections 继续创建。
	if (SessionInterface->GetNamedSession(NAME_GameSession))
	{
		bCreateSessionAfterDestroy = true;
		DestroySession();
		return;
	}

	// Settings 必须作为成员保存，确保其生命周期覆盖 OnlineSubsystem 的异步创建过程。
	LastSessionSettings = MakeShared<FOnlineSessionSettings>();

	// 只开放公开槽位；当前项目没有密码邀请对应的私有连接需求。
	LastSessionSettings->NumPublicConnections = PendingNumPublicConnections;
	LastSessionSettings->NumPrivateConnections = 0;

	// 允许服务器被搜索到，并允许会话进入 InProgress 后仍有新玩家加入。
	LastSessionSettings->bShouldAdvertise = true;
	LastSessionSettings->bAllowJoinInProgress = true;

	// Null OSS 通过局域网 Beacon 广告/搜索；Dedicated 标志必须反映当前 World 的实际 NetMode。
	LastSessionSettings->bIsLANMatch = IsUsingNullSubsystem();
	LastSessionSettings->bIsDedicated = IsDedicatedServerInstance();

	// 本项目暂不使用平台统计、邀请、Presence、Lobby、语音或反作弊服务。
	// 显式赋值可避免未来切换 OSS 时无意继承平台默认行为。
	LastSessionSettings->bUsesStats = false;
	LastSessionSettings->bAllowInvites = false;
	LastSessionSettings->bUsesPresence = false;
	LastSessionSettings->bAllowJoinViaPresence = false;
	LastSessionSettings->bAllowJoinViaPresenceFriendsOnly = false;
	LastSessionSettings->bAntiCheatProtected = false;
	LastSessionSettings->bUseLobbiesIfAvailable = false;
	LastSessionSettings->bUseLobbiesVoiceChatIfAvailable = false;

	// 搜索时通常只显示相同 BuildUniqueId 的服务器；打包版本策略确定后可改为项目构建号。
	LastSessionSettings->BuildUniqueId = 1;

	// 把当前地图写入会话广告，供服务器浏览器展示或筛选。
	// PIE World 名称可能带 UEDPIE_x_ 前缀，需要移除后再发布。
	if (const UWorld* World = GetWorld())
	{
		FString MapName = World->GetMapName();
		MapName.RemoveFromStart(World->StreamingLevelsPrefix);
		LastSessionSettings->Set(
			MultiplayerSessionsConstants::MapNameSettingKey,
			MapName,
			EOnlineDataAdvertisementType::ViaOnlineService);
	}

	// 必须先注册完成回调再发起请求，避免极快完成的 OSS 实现丢失通知。
	// 返回的 Handle 用于完成、失败或 Deinitialize 时精确解除本次注册。
	CreateSessionCompleteDelegateHandle = SessionInterface->AddOnCreateSessionCompleteDelegate_Handle(
		CreateSessionCompleteDelegate);

	// 返回 true 仅表示异步请求成功启动，不代表会话已经创建成功。
	// 最终结果由 HandleCreateSessionComplete 接收。
	if (!SessionInterface->CreateSession(
		MultiplayerSessionsConstants::LocalUserNumber,
		NAME_GameSession,
		*LastSessionSettings))
	{
		// 立即返回 false 时 OSS 不会再触发完成回调，因此必须在这里自行解绑并广播失败。
		SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
		CreateSessionCompleteDelegateHandle.Reset();
		LastSessionSettings.Reset();
		UE_LOG(LogMultiplayerSessions, Error, TEXT("CreateSession failed to start."));
		MultiplayerOnCreateSessionComplete.Broadcast(false);
		return;
	}

	UE_LOG(
		LogMultiplayerSessions,
		Log,
		TEXT("Creating %s session with %d public connections."),
		IsDedicatedServerInstance() ? TEXT("dedicated") : TEXT("listen"),
		PendingNumPublicConnections);
}

void UMultiplayerSessionsSubsystem::FindSessions(const int32 MaxSearchResults)
{
	// 接口无效属于“请求未启动”，仍通过统一完成委托通知调用者，避免 UI 一直等待。
	if (!RefreshSessionInterface())
	{
		MultiplayerOnFindSessionsComplete.Broadcast(TArray<FOnlineSessionSearchResult>(), false);
		return;
	}

	// LastSessionSearch 和 Handle 都是单份成员状态，因此同一时刻只允许一个搜索请求。
	if (FindSessionsCompleteDelegateHandle.IsValid())
	{
		UE_LOG(LogMultiplayerSessions, Warning, TEXT("FindSessions ignored: another search request is active."));
		MultiplayerOnFindSessionsComplete.Broadcast(TArray<FOnlineSessionSearchResult>(), false);
		return;
	}

	// 搜索对象必须存活到异步完成；结果也保存在该对象中，供随后 JoinSession 使用。
	LastSessionSearch = MakeShared<FOnlineSessionSearch>();
	LastSessionSearch->MaxSearchResults = FMath::Clamp(
		MaxSearchResults,
		1,
		MultiplayerSessionsConstants::MaxSupportedSearchResults);
	// OnlineSubsystemNull 依靠 LAN Beacon；其他平台 OSS 使用其在线服务查询。
	LastSessionSearch->bIsLanQuery = IsUsingNullSubsystem();

	// 与 CreateSession 相同：先注册完成委托，再发起异步搜索。
	FindSessionsCompleteDelegateHandle = SessionInterface->AddOnFindSessionsCompleteDelegate_Handle(
		FindSessionsCompleteDelegate);

	// false 表示搜索没有启动，不会再收到异步完成事件，必须立即清理并主动广播失败。
	if (!SessionInterface->FindSessions(
		MultiplayerSessionsConstants::LocalUserNumber,
		LastSessionSearch.ToSharedRef()))
	{
		SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
		FindSessionsCompleteDelegateHandle.Reset();
		LastSessionSearch.Reset();
		MultiplayerOnFindSessionsComplete.Broadcast(TArray<FOnlineSessionSearchResult>(), false);
	}
}

void UMultiplayerSessionsSubsystem::JoinSession(const FOnlineSessionSearchResult& SearchResult)
{
	// JoinSession 依赖 FindSessions 返回的原始 FOnlineSessionSearchResult。
	// 接口无效或请求未启动时统一使用 UnknownError 通知上层。
	if (!RefreshSessionInterface())
	{
		MultiplayerOnJoinSessionComplete.Broadcast(EOnJoinSessionCompleteResult::UnknownError);
		return;
	}

	// 防止后一次 Join 覆盖前一次请求的完成句柄。
	if (JoinSessionCompleteDelegateHandle.IsValid())
	{
		UE_LOG(LogMultiplayerSessions, Warning, TEXT("JoinSession ignored: another join request is active."));
		MultiplayerOnJoinSessionComplete.Broadcast(EOnJoinSessionCompleteResult::UnknownError);
		return;
	}

	// 先注册后请求，HandleJoinSessionComplete 负责最终清理和结果广播。
	JoinSessionCompleteDelegateHandle = SessionInterface->AddOnJoinSessionCompleteDelegate_Handle(
		JoinSessionCompleteDelegate);

	// Join 成功只会在本地建立 NAME_GameSession 并准备连接信息；这里不会自动 ClientTravel。
	// 调用方应在成功回调中调用 GetResolvedConnectString，再交给 PlayerController::ClientTravel。
	if (!SessionInterface->JoinSession(
		MultiplayerSessionsConstants::LocalUserNumber,
		NAME_GameSession,
		SearchResult))
	{
		// 请求未启动时不会收到完成回调，因此立即解除注册并报告 UnknownError。
		SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
		JoinSessionCompleteDelegateHandle.Reset();
		MultiplayerOnJoinSessionComplete.Broadcast(EOnJoinSessionCompleteResult::UnknownError);
	}
}

void UMultiplayerSessionsSubsystem::DestroySession()
{
	// 刷新失败时无法判断或修改 OSS 会话状态，同时取消可能存在的“销毁后重建”意图。
	if (!RefreshSessionInterface())
	{
		bCreateSessionAfterDestroy = false;
		MultiplayerOnDestroySessionComplete.Broadcast(false);
		return;
	}

	// 重复调用销毁不会开启第二个异步请求，也不会覆盖第一个请求的 Handle。
	if (DestroySessionCompleteDelegateHandle.IsValid())
	{
		UE_LOG(LogMultiplayerSessions, Warning, TEXT("DestroySession ignored: another destroy request is active."));
		return;
	}

	// 当前已经没有 NAME_GameSession，可把 Destroy 的目标视为已经成功达成。
	// 如果这是 CreateSession 触发的重建流程，则无需等待 OSS，直接继续创建。
	if (!SessionInterface->GetNamedSession(NAME_GameSession))
	{
		const bool bShouldCreateSession = bCreateSessionAfterDestroy;
		bCreateSessionAfterDestroy = false;
		MultiplayerOnDestroySessionComplete.Broadcast(true);
		if (bShouldCreateSession)
		{
			CreateSession(PendingNumPublicConnections);
		}
		return;
	}

	// 只有确实存在会话时才注册异步销毁完成回调。
	DestroySessionCompleteDelegateHandle = SessionInterface->AddOnDestroySessionCompleteDelegate_Handle(
		DestroySessionCompleteDelegate);

	// false 表示请求没有启动：清理句柄、取消重建，避免随后误创建新会话。
	if (!SessionInterface->DestroySession(NAME_GameSession))
	{
		SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegateHandle);
		DestroySessionCompleteDelegateHandle.Reset();
		bCreateSessionAfterDestroy = false;
		MultiplayerOnDestroySessionComplete.Broadcast(false);
	}
}

void UMultiplayerSessionsSubsystem::StartSession()
{
	// StartSession 只推进 Online Session 状态，不负责地图切换或启动网络监听。
	if (!RefreshSessionInterface())
	{
		MultiplayerOnStartSessionComplete.Broadcast(false);
		return;
	}

	// 一个会话不需要同时执行多个 Start 请求。
	if (StartSessionCompleteDelegateHandle.IsValid())
	{
		UE_LOG(LogMultiplayerSessions, Warning, TEXT("StartSession ignored: another start request is active."));
		return;
	}

	// Start 必须建立在已经成功创建的本地命名会话之上。
	if (!SessionInterface->GetNamedSession(NAME_GameSession))
	{
		UE_LOG(LogMultiplayerSessions, Warning, TEXT("StartSession failed: GameSession does not exist."));
		MultiplayerOnStartSessionComplete.Broadcast(false);
		return;
	}

	// 注册完成回调后再发起请求；完成时由 HandleStartSessionComplete 清理。
	StartSessionCompleteDelegateHandle = SessionInterface->AddOnStartSessionCompleteDelegate_Handle(
		StartSessionCompleteDelegate);

	// 请求未启动时不会收到异步结果，因此立即清理并广播失败。
	if (!SessionInterface->StartSession(NAME_GameSession))
	{
		SessionInterface->ClearOnStartSessionCompleteDelegate_Handle(StartSessionCompleteDelegateHandle);
		StartSessionCompleteDelegateHandle.Reset();
		MultiplayerOnStartSessionComplete.Broadcast(false);
	}
}

bool UMultiplayerSessionsSubsystem::GetResolvedConnectString(FString& ConnectString) const
{
	// NAME_GameSession 在 Join 成功后保存平台解析出的目标地址。
	// Null OSS 通常得到 IP:Port；Steam/EOS 等平台可能返回平台专用连接字符串。
	return SessionInterface.IsValid()
		&& SessionInterface->GetResolvedConnectString(NAME_GameSession, ConnectString);
}

bool UMultiplayerSessionsSubsystem::IsDedicatedServerInstance() const
{
	// 不能只检查是否编译了 Server Target；最终依据是当前 World 的实际 NetMode。
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() == NM_DedicatedServer;
}

bool UMultiplayerSessionsSubsystem::RefreshSessionInterface()
{
	// Online::GetSessionInterface(World) 会依据 World Context 选择正确的 OSS 实例，
	// 对同进程多 PIE World 比 IOnlineSubsystem::Get() 更安全。
	const IOnlineSessionPtr WorldSessionInterface = Online::GetSessionInterface(GetWorld());
	if (WorldSessionInterface != SessionInterface)
	{
		// 句柄只能交还给产生它的旧 SessionInterface；必须在替换共享指针前清理。
		ClearOnlineDelegates();
		SessionInterface = WorldSessionInterface;
	}

	return SessionInterface.IsValid();
}

void UMultiplayerSessionsSubsystem::ClearOnlineDelegates()
{
	// 接口已经无效时无法调用 Clear...，但本地 Handle 仍要 Reset，
	// 否则 IsValid() 会让后续逻辑误判为仍有异步请求正在进行。
	if (!SessionInterface.IsValid())
	{
		CreateSessionCompleteDelegateHandle.Reset();
		FindSessionsCompleteDelegateHandle.Reset();
		JoinSessionCompleteDelegateHandle.Reset();
		DestroySessionCompleteDelegateHandle.Reset();
		StartSessionCompleteDelegateHandle.Reset();
		return;
	}

	// 每一类异步操作使用独立 Handle，必须调用与注册 API 一一对应的 Clear 函数。
	if (CreateSessionCompleteDelegateHandle.IsValid())
	{
		SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
		CreateSessionCompleteDelegateHandle.Reset();
	}
	if (FindSessionsCompleteDelegateHandle.IsValid())
	{
		SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
		FindSessionsCompleteDelegateHandle.Reset();
	}
	if (JoinSessionCompleteDelegateHandle.IsValid())
	{
		SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
		JoinSessionCompleteDelegateHandle.Reset();
	}
	if (DestroySessionCompleteDelegateHandle.IsValid())
	{
		SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegateHandle);
		DestroySessionCompleteDelegateHandle.Reset();
	}
	if (StartSessionCompleteDelegateHandle.IsValid())
	{
		SessionInterface->ClearOnStartSessionCompleteDelegate_Handle(StartSessionCompleteDelegateHandle);
		StartSessionCompleteDelegateHandle.Reset();
	}
}

bool UMultiplayerSessionsSubsystem::IsUsingNullSubsystem() const
{
	// Null OSS 没有互联网后端，通过 LAN Beacon 发现 bIsLANMatch 会话。
	// 让创建和搜索两侧使用相同判断，避免一侧发布 LAN、另一侧查询 Online。
	const IOnlineSubsystem* OnlineSubsystem = Online::GetSubsystem(GetWorld());
	return OnlineSubsystem && OnlineSubsystem->GetSubsystemName() == NULL_SUBSYSTEM;
}

void UMultiplayerSessionsSubsystem::HandleCreateSessionComplete(
	const FName SessionName,
	const bool bWasSuccessful)
{
	// 完成回调的第一件事是解除 OSS 注册。这样外部监听者在 Broadcast 中再次调用
	// CreateSession 时，不会被旧 Handle 误判为“仍有创建请求正在执行”。
	if (SessionInterface.IsValid())
	{
		SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
	}
	CreateSessionCompleteDelegateHandle.Reset();

	if (bWasSuccessful)
	{
		UE_LOG(LogMultiplayerSessions, Log, TEXT("CreateSession completed for %s: success."), *SessionName.ToString());
	}
	else
	{
		UE_LOG(LogMultiplayerSessions, Error, TEXT("CreateSession completed for %s: failure."), *SessionName.ToString());
	}

	// 将底层 OSS 结果转换为插件统一事件，供 C++ 或 Blueprint UI 使用。
	MultiplayerOnCreateSessionComplete.Broadcast(bWasSuccessful);

	// Dedicated Server 没有菜单负责启动会话；创建成功后按配置推进到 InProgress。
	// 普通客户端/Listen Server 不在这里自动 Start，以便由其主机流程决定旅行时机。
	if (bWasSuccessful && IsDedicatedServerInstance() && bAutoStartDedicatedSession)
	{
		StartSession();
	}
}

void UMultiplayerSessionsSubsystem::HandleFindSessionsComplete(const bool bWasSuccessful)
{
	// 先清理注册，允许监听者在下面的 Broadcast 中立即发起下一次搜索。
	if (SessionInterface.IsValid())
	{
		SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
	}
	FindSessionsCompleteDelegateHandle.Reset();

	// 正常情况下 LastSessionSearch 一直存活到此处；额外检查可覆盖退出或接口切换等边界情况。
	// 即使 OSS 报告成功，只要搜索对象已失效，对外仍应报告失败并返回空数组。
	const bool bHasValidSearch = LastSessionSearch.IsValid();
	const TArray<FOnlineSessionSearchResult> EmptyResults;
	MultiplayerOnFindSessionsComplete.Broadcast(
		bHasValidSearch ? LastSessionSearch->SearchResults : EmptyResults,
		bWasSuccessful && bHasValidSearch);
}

void UMultiplayerSessionsSubsystem::HandleJoinSessionComplete(
	const FName SessionName,
	const EOnJoinSessionCompleteResult::Type Result)
{
	// 清理后再广播，使监听者能够在回调中安全重试 JoinSession。
	if (SessionInterface.IsValid())
	{
		SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
	}
	JoinSessionCompleteDelegateHandle.Reset();

	if (Result == EOnJoinSessionCompleteResult::Success)
	{
		UE_LOG(LogMultiplayerSessions, Log, TEXT("JoinSession completed for %s: success."), *SessionName.ToString());
	}
	else
	{
		UE_LOG(
			LogMultiplayerSessions,
			Warning,
			TEXT("JoinSession completed for %s with result %d."),
			*SessionName.ToString(),
			static_cast<int32>(Result));
	}

	// 子系统不擅自进行 ClientTravel；成功监听者可调用 GetResolvedConnectString 决定旅行策略。
	MultiplayerOnJoinSessionComplete.Broadcast(Result);
}

void UMultiplayerSessionsSubsystem::HandleDestroySessionComplete(
	const FName SessionName,
	const bool bWasSuccessful)
{
	// 清理销毁 Handle 后，后续重建调用才不会被视为仍有销毁请求。
	if (SessionInterface.IsValid())
	{
		SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegateHandle);
	}
	DestroySessionCompleteDelegateHandle.Reset();

	if (bWasSuccessful)
	{
		UE_LOG(LogMultiplayerSessions, Log, TEXT("DestroySession completed for %s: success."), *SessionName.ToString());
	}
	else
	{
		UE_LOG(LogMultiplayerSessions, Error, TEXT("DestroySession completed for %s: failure."), *SessionName.ToString());
	}

	// 只在旧会话确实销毁成功时继续重建；无论结果如何都要消费并复位一次性意图。
	const bool bShouldCreateSession = bWasSuccessful && bCreateSessionAfterDestroy;
	bCreateSessionAfterDestroy = false;
	MultiplayerOnDestroySessionComplete.Broadcast(bWasSuccessful);

	// 使用 CreateSession 调用开始一条全新的异步链路，并复用之前保存的公开槽位数。
	if (bShouldCreateSession)
	{
		CreateSession(PendingNumPublicConnections);
	}
}

void UMultiplayerSessionsSubsystem::HandleStartSessionComplete(
	const FName SessionName,
	const bool bWasSuccessful)
{
	// 先解绑再对外广播，保证监听者可以在回调里安全执行后续会话操作。
	if (SessionInterface.IsValid())
	{
		SessionInterface->ClearOnStartSessionCompleteDelegate_Handle(StartSessionCompleteDelegateHandle);
	}
	StartSessionCompleteDelegateHandle.Reset();

	if (bWasSuccessful)
	{
		UE_LOG(LogMultiplayerSessions, Log, TEXT("StartSession completed for %s: success."), *SessionName.ToString());
	}
	else
	{
		UE_LOG(LogMultiplayerSessions, Error, TEXT("StartSession completed for %s: failure."), *SessionName.ToString());
	}

	// 向插件外部统一报告会话是否已经进入可运行状态。
	MultiplayerOnStartSessionComplete.Broadcast(bWasSuccessful);
}
