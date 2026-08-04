// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MultiplayerSessionsSubsystem.generated.h"

/**
 * 对外广播“创建会话完成”的动态多播委托。
 * 参数能够被 UE 反射，所以可通过 BlueprintAssignable 暴露为蓝图 Event Dispatcher。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FMultiplayerOnCreateSessionComplete,
	bool,
	bWasSuccessful);

/**
 * 对外广播“搜索会话完成”的原生多播委托。
 * FOnlineSessionSearchResult 不是蓝图类型，因此本委托只面向 C++ 监听者。
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FMultiplayerOnFindSessionsComplete,
	const TArray<FOnlineSessionSearchResult>&,
	bool);

/**
 * 对外广播“加入会话完成”的原生多播委托。
 * EOnJoinSessionCompleteResult::Type 能区分成功、会话已满、会话不存在等结果。
 */
DECLARE_MULTICAST_DELEGATE_OneParam(
	FMultiplayerOnJoinSessionComplete,
	EOnJoinSessionCompleteResult::Type);

/** 对外广播“销毁会话完成”的动态多播委托。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FMultiplayerOnDestroySessionComplete,
	bool,
	bWasSuccessful);

/** 对外广播“启动会话完成”的动态多播委托。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FMultiplayerOnStartSessionComplete,
	bool,
	bWasSuccessful);

/**
 * 统一封装 Online Session 的异步创建、搜索、加入、销毁和启动流程。
 *
 * Dedicated Server 可以在 Initialize 时根据 DefaultGame.ini 自动创建会话；
 * 客户端也可以完全绕过本子系统，继续通过 IP 地址直接连接服务器。
 */
UCLASS()
class MULTIPLAYERSESSIONS_API UMultiplayerSessionsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * GameInstance 创建本子系统时调用。
	 * 初始化五类 OSS 完成委托；Dedicated Server 还会读取配置并按需自动创建会话。
	 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/**
	 * GameInstance 销毁本子系统前调用。
	 * 解除仍注册在 OnlineSubsystem 上的所有委托，避免残留回调进入失效对象。
	 */
	virtual void Deinitialize() override;

	/**
	 * 创建 NAME_GameSession。
	 *
	 * 如果已有同名会话，会先异步销毁旧会话，再使用本次参数重新创建。
	 * 请求能否成功启动与最终异步结果都会通过 MultiplayerOnCreateSessionComplete 报告。
	 *
	 * @param NumPublicConnections 公开连接槽位数，运行时限制到 1~64。
	 */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Sessions", meta = (ClampMin = "1"))
	void CreateSession(int32 NumPublicConnections = 8);

	/**
	 * 异步搜索可加入的会话。
	 * Null OSS 使用 LAN 查询；结果通过 MultiplayerOnFindSessionsComplete 返回。
	 *
	 * @param MaxSearchResults 最多保存的搜索结果数，运行时限制到 1~1000。
	 */
	void FindSessions(int32 MaxSearchResults = 100);

	/**
	 * 异步加入指定搜索结果。
	 * 成功只代表 OSS 已建立命名会话；调用方仍需获取连接地址并执行 ClientTravel。
	 *
	 * @param SearchResult FindSessions 返回的一个有效搜索结果。
	 */
	void JoinSession(const FOnlineSessionSearchResult& SearchResult);

	/**
	 * 异步销毁 NAME_GameSession。
	 * 如果当前没有会话，按“目标状态已经达成”处理为成功。
	 */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Sessions")
	void DestroySession();

	/**
	 * 将已创建的 NAME_GameSession 从 Pending 推进到 InProgress。
	 * Dedicated Server 自动创建成功后会根据配置自动调用本函数。
	 */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Sessions")
	void StartSession();

	/**
	 * 获取 JoinSession 成功后可供 APlayerController::ClientTravel 使用的地址。
	 *
	 * @param ConnectString 成功时写入解析出的 IP/端口或平台连接字符串。
	 * @return SessionInterface 有效且地址解析成功时返回 true。
	 */
	bool GetResolvedConnectString(FString& ConnectString) const;

	/** 根据当前 World 的 NetMode 判断本实例是否为真正运行中的 Dedicated Server。 */
	bool IsDedicatedServerInstance() const;

	/** 创建请求结束时向 C++ 和 Blueprint 监听者广播。 */
	UPROPERTY(BlueprintAssignable, Category = "Multiplayer Sessions")
	FMultiplayerOnCreateSessionComplete MultiplayerOnCreateSessionComplete;

	/** 搜索请求结束时向 C++ 监听者广播搜索结果和成功标记。 */
	FMultiplayerOnFindSessionsComplete MultiplayerOnFindSessionsComplete;

	/** 加入请求结束时向 C++ 监听者广播精确的加入结果。 */
	FMultiplayerOnJoinSessionComplete MultiplayerOnJoinSessionComplete;

	/** 销毁请求结束时向 C++ 和 Blueprint 监听者广播。 */
	UPROPERTY(BlueprintAssignable, Category = "Multiplayer Sessions")
	FMultiplayerOnDestroySessionComplete MultiplayerOnDestroySessionComplete;

	/** 启动请求结束时向 C++ 和 Blueprint 监听者广播。 */
	UPROPERTY(BlueprintAssignable, Category = "Multiplayer Sessions")
	FMultiplayerOnStartSessionComplete MultiplayerOnStartSessionComplete;

private:
	/**
	 * 按当前 World Context 重新获取 IOnlineSession。
	 * PIE 多 World 或 OSS 实例变化时，会先从旧接口解除所有委托再切换。
	 */
	bool RefreshSessionInterface();

	/** 从当前 SessionInterface 解除所有仍有效的 OSS 完成委托，并重置句柄。 */
	void ClearOnlineDelegates();

	/** 当前 World 使用 OnlineSubsystemNull 时返回 true，用于决定是否采用 LAN 会话/查询。 */
	bool IsUsingNullSubsystem() const;

	/** OSS 创建完成回调：清理底层句柄、向外广播，并按配置启动 Dedicated 会话。 */
	void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful);

	/** OSS 搜索完成回调：清理底层句柄并广播 LastSessionSearch 中保存的结果。 */
	void HandleFindSessionsComplete(bool bWasSuccessful);

	/** OSS 加入完成回调：清理底层句柄并广播加入结果。 */
	void HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);

	/** OSS 销毁完成回调：清理底层句柄，并在需要时继续“销毁后重建”流程。 */
	void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	/** OSS 启动完成回调：清理底层句柄并向外广播结果。 */
	void HandleStartSessionComplete(FName SessionName, bool bWasSuccessful);

	/** 当前 World 对应的 Online Session 接口；所有会话操作都经由此接口发起。 */
	IOnlineSessionPtr SessionInterface;

	/**
	 * 最近一次创建请求的设置。
	 * 作为成员保存，确保 FOnlineSessionSettings 的生命周期覆盖异步创建过程。
	 */
	TSharedPtr<FOnlineSessionSettings> LastSessionSettings;

	/**
	 * 最近一次搜索请求及其结果。
	 * 完成广播后仍保留，使调用方可以从结果中选择一个会话并调用 JoinSession。
	 */
	TSharedPtr<FOnlineSessionSearch> LastSessionSearch;

	/** 交给 OSS 的原生单播创建完成回调。 */
	FOnCreateSessionCompleteDelegate CreateSessionCompleteDelegate;
	/** 创建完成回调在 OSS 内部多播列表中的注册标识，同时也表示创建请求正在进行。 */
	FDelegateHandle CreateSessionCompleteDelegateHandle;

	/** 交给 OSS 的原生单播搜索完成回调。 */
	FOnFindSessionsCompleteDelegate FindSessionsCompleteDelegate;
	/** 搜索完成回调的注册标识，同时也表示搜索请求正在进行。 */
	FDelegateHandle FindSessionsCompleteDelegateHandle;

	/** 交给 OSS 的原生单播加入完成回调。 */
	FOnJoinSessionCompleteDelegate JoinSessionCompleteDelegate;
	/** 加入完成回调的注册标识，同时也表示加入请求正在进行。 */
	FDelegateHandle JoinSessionCompleteDelegateHandle;

	/** 交给 OSS 的原生单播销毁完成回调。 */
	FOnDestroySessionCompleteDelegate DestroySessionCompleteDelegate;
	/** 销毁完成回调的注册标识，同时也表示销毁请求正在进行。 */
	FDelegateHandle DestroySessionCompleteDelegateHandle;

	/** 交给 OSS 的原生单播启动完成回调。 */
	FOnStartSessionCompleteDelegate StartSessionCompleteDelegate;
	/** 启动完成回调的注册标识，同时也表示启动请求正在进行。 */
	FDelegateHandle StartSessionCompleteDelegateHandle;

	/** 当前创建请求期望使用的公开槽位数；销毁旧会话期间也必须保存该值。 */
	int32 PendingNumPublicConnections = 8;

	/** 标记当前销毁操作是否由 CreateSession 的“先销毁后重建”流程触发。 */
	bool bCreateSessionAfterDestroy = false;

	/** Dedicated Server 是否在创建成功后自动调用 StartSession；读取自 DefaultGame.ini。 */
	bool bAutoStartDedicatedSession = true;
};
