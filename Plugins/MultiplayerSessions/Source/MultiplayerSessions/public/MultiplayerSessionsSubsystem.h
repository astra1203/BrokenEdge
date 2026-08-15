// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Subsystems/GameInstanceSubsystem.h"
// #include "OnlineSessionSettings.h"
#include "MultiplayerSessionsSubsystem.generated.h"


/**
 * 会话创建完成委托
 * @param bWasSuccessful - 是否成功创建会话
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMultiplayerOnCreateSessionComplete, bool, bWasSuccessful);

/**
 * 会话查找完成委托
 * @param SessionResults - 查找到的会话结果数组
 * @param bWasSuccessful - 是否成功查找到会话
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FMultiplayerOnFindSessionsComplete,
                                     const TArray<FOnlineSessionSearchResult>& SessionResults, bool bWasSuccessful);

/**
 * 会话加入完成委托
 * @param Result - 加入会话的结果类型
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FMultiplayerOnJoinSessionComplete, EOnJoinSessionCompleteResult::Type Result);

/**
 * 会话销毁完成委托
 * @param bWasSuccessful - 是否成功销毁会话
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMultiplayerOnDestorySessionComplete, bool, bWasSuccessful);

/**
 * 会话启动完成委托
 * @param bWasSuccessful - 是否成功启动会话
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMultiplayerOnStartSessionComplete, bool, bWasSuccessful);

/**
 * 
 */
UCLASS()
class MULTIPLAYERSESSIONS_API UMultiplayerSessionsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UMultiplayerSessionsSubsystem();

	/** GameInstance 创建子系统时获取当前 World 对应的 OSS 接口。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// virtual void Deinitialize() override;

	/** 创建一个新的在线会话（专用服务器）。 */
	void CreateSession(int32 NumPublicConnections, FString MatchType);

	/** 查找可用的在线会话（专用服务器模式下仅搜索专用服务器）。 */
	void FindSessions(int32 MaxSearchResults);

	/** 加入一个已找到的会话。 */
	void JoinSession(const FOnlineSessionSearchResult& SessionResult);
	
	/** 销毁当前会话。 */
	void DestorySession();

	// /** 销毁当前会话。 */
	// void DestorySession();
	//

	/** 多播委托 - 供外部监听会话事件。 */
	FMultiplayerOnCreateSessionComplete MultiplayerOnCreateSessionComplete;
	FMultiplayerOnFindSessionsComplete MultiplayerOnFindSessionsComplete;
	FMultiplayerOnJoinSessionComplete MultiplayerOnJoinSessionComplete;
	FMultiplayerOnDestorySessionComplete MultiplayerOnDestorySessionComplete;

	/** 表示创建的会话（房间）支持多少在线玩家 */
	int32 DesiredNumPublicConnections = 8;
	FString DesiredMatchType = TEXT("FreeForAll");

protected:
	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);

	// 搜索请求结束后自动调用这里；搜索到的房间已经被底层写进 LastSessionSearch->SearchResults。
	void OnFindSessionsComplete(bool bWasSuccessful);


private:
	/** 按当前 World 重新获取在线会话接口；发生变化时先清理旧委托。 */
	bool RefreshSessionInterface();

	/** 依据当前 World 的 NetMode 判断，而不是假设 Server Target 一定已经进入 Dedicated World。 */
	bool IsDedicatedServerInstance() const;

	/** 当前 World 的 Online Session 接口。World/PIE 实例变化时会刷新。 */
	IOnlineSessionPtr SessionInterface;

	/** 必须跨越异步 CreateSession 调用保存，直至完成回调。 */
	TSharedPtr<FOnlineSessionSettings> LastSessionSettings;
	TSharedPtr<FOnlineSessionSearch> LastSessionSearch;

	
	// 每项异步操作各自持有一个句柄，便于防止重复请求并精确解绑。
	FOnCreateSessionCompleteDelegate CreateSessionCompleteDelegate;
	FDelegateHandle CreateSessionCompleteDelegateHandle;
	
	FOnFindSessionsCompleteDelegate FindSessionsCompleteDelegate;
	FDelegateHandle FindSessionsCompleteDelegateHandle;

	FOnJoinSessionCompleteDelegate JoinSessionCompleteDelegate;
	FDelegateHandle JoinSessionCompleteDelegateHandle;

	FOnDestroySessionCompleteDelegate DestorySessionCompleteDelegate;
	FDelegateHandle DestorySessionCompleteDelegateHandle;

	/** 创建请求需要先销毁旧会话时，暂存新的公开连接数。 */
	// int32 PendingNumPublicConnections = 8;

	/** 保存最近一次搜索及其原生结果，供蓝图结果索引和 C++ 调用方使用。 */
	// TSharedPtr<FOnlineSessionSearch> LastSessionSearch;

	/** 每开始一次新搜索就递增，用于识别 UI 保存的过期结果。 */
	// int32 SearchGeneration = 0;
	bool bCreateSessionOnDestory = false;
	int32 LastNumPublicConnections = 8;
	FString LastMatchType = TEXT("FreeForAll");
};
