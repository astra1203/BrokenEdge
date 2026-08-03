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
	
	CreateSession();
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

bool UMultiplayerSessionsSubsystem::ValidateNullSubsystem(const TCHAR* OperationName) const
{
	// 本插件当前按用户选择明确锁定 OnlineSubsystemNull。
	// 显式校验能让 DefaultPlatformService 拼写或插件启用错误尽早暴露在日志和失败事件中。
	const IOnlineSubsystem* OnlineSubsystem = Online::GetSubsystem(GetWorld());
	if (OnlineSubsystem && OnlineSubsystem->GetSubsystemName() == NULL_SUBSYSTEM)
	{
		return true;
	}

	UE_LOG(
		LogMultiplayerSessions,
		Error,
		TEXT("%s failed: OnlineSubsystemNull is not active (active subsystem: %s)."),
		OperationName,
		OnlineSubsystem ? *OnlineSubsystem->GetSubsystemName().ToString() : TEXT("None"));
	return false;
}

void UMultiplayerSessionsSubsystem::CreateSession(int32 NumPublicConnections)
{
	// Blueprint 元数据只能约束蓝图输入，C++ 调用仍可能传入任意值，因此运行时再次 Clamp。
	PendingNumPublicConnections = FMath::Clamp(
		NumPublicConnections,
		1,
		MultiplayerSessionsConstants::MaxSupportedPublicConnections);
	
	// 本实现明确采用 Dedicated Server 主机模式，不允许客户端借此函数悄悄建立 Listen Server。
	if (!IsDedicatedServerInstance())
	{
		UE_LOG(LogMultiplayerSessions, Warning, TEXT("CreateSession is restricted to a Dedicated Server instance."));
		BroadcastCreateSessionComplete(false);
		return;
	}

	// 先刷新当前 World 的接口，再确认配置使用 Null；任一步失败都走同一套完成事件。
	if (!RefreshSessionInterface() || !ValidateNullSubsystem(TEXT("CreateSession")))
	{
		BroadcastCreateSessionComplete(false);
		return;
	}

	// // Create 和 Destroy 会修改同一个 NAME_GameSession，禁止两种请求并发可以避免状态竞争和委托句柄覆盖。
	// if (CreateSessionCompleteDelegateHandle.IsValid() || DestroySessionCompleteDelegateHandle.IsValid())
	// {
	// 	UE_LOG(LogMultiplayerSessions, Warning, TEXT("CreateSession ignored because another create/destroy request is active."));
	// 	BroadcastCreateSessionComplete(false);
	// 	return;
	// }
	//
	// // OSS 不允许用相同 FName 重复创建会话。记录“销毁后重建”意图，
	// // 等 DestroySession 的异步完成回调成功后再继续本次 CreateSession。
	// if (SessionInterface->GetNamedSession(NAME_GameSession))
	// {
	// 	bCreateSessionAfterDestroy = true;
	// 	DestroySession();
	// 	return;
	// }
	
	// Settings 必须保存为成员，确保其生命周期覆盖整个异步创建过程。
	LastSessionSettings = MakeShared<FOnlineSessionSettings>();

	// 只开放公开连接，不使用邀请/密码对应的私有连接。
	LastSessionSettings->NumPublicConnections = PendingNumPublicConnections;
	LastSessionSettings->NumPrivateConnections = 0;

	// Null 通过 LAN Beacon 广播 bShouldAdvertise 的 LAN 会话。
	// 自动启动后状态会进入 InProgress，bAllowJoinInProgress=true 保证仍继续响应搜索和加入。
	LastSessionSettings->bShouldAdvertise = true;
	LastSessionSettings->bAllowJoinInProgress = true;
	LastSessionSettings->bIsLANMatch = true;
	LastSessionSettings->bIsDedicated = true;

	// 当前需求不使用统计、邀请、Presence、Lobby、语音房间或反作弊服务；
	// 明确赋值可以让未来切换 OSS 时仍保持当前会话规则，而不是依赖平台默认值。
	LastSessionSettings->bUsesStats = false;
	LastSessionSettings->bAllowInvites = false;
	LastSessionSettings->bUsesPresence = false;
	LastSessionSettings->bAllowJoinViaPresence = false;
	LastSessionSettings->bAllowJoinViaPresenceFriendsOnly = false;
	LastSessionSettings->bAntiCheatProtected = false;
	LastSessionSettings->bUseLobbiesIfAvailable = false;
	LastSessionSettings->bUseLobbiesVoiceChatIfAvailable = false;
	
	if (!SessionInterface->CreateSession(
		MultiplayerSessionsConstants::LocalUserNumber,
		NAME_GameSession,
		*LastSessionSettings))
	{
		// 请求未启动时 OSS 不会再触发回调，必须在这里立即解绑并主动广播失败。
		SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
		CreateSessionCompleteDelegateHandle.Reset();
		LastSessionSettings.Reset();
		BroadcastCreateSessionComplete(false);
		return;
	}
	
	UE_LOG(
	LogMultiplayerSessions,
	Log,
	TEXT("Oreki Dedicated server is creating a Null LAN session with %d public connections."),
	PendingNumPublicConnections);
}
