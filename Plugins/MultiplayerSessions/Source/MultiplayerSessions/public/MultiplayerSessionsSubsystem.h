// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MultiplayerSessionsSubsystem.generated.h"




/**
 * 
 */
UCLASS()
class MULTIPLAYERSESSIONS_API UMultiplayerSessionsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	/** GameInstance 创建子系统时获取当前 World 对应的 OSS 接口。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	
	
	/** 依据当前 World 的 NetMode 判断，而不是假设 Server Target 一定已经进入 Dedicated World。 */
	bool IsDedicatedServerInstance() const;
	
	// 统一广播入口，保证“立即发起失败”和“异步完成”走相同的 Blueprint/C++ 事件路径。
	void BroadcastCreateSessionComplete(bool bWasSuccessful);
	
	/** 获取当前 World 对应的 SessionInterface；PIE 多 World 时不能只使用全局默认实例。 */
	bool RefreshSessionInterface();
	
	/** 确认当前活动服务确实是 Null，防止配置错误时静默使用另一套 OSS 行为。 */
	bool ValidateNullSubsystem(const TCHAR* OperationName) const;
	
	/**
 * 在 Dedicated Server 上创建 NAME_GameSession。
 *
 * 如果已经存在同名会话，会先异步销毁旧会话，销毁成功后自动继续本次创建请求。
 * NumPublicConnections 会限制到 1~64；客户端或 Listen Server 调用会收到失败事件。
 */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Sessions", meta = (ClampMin = "1"))
	void CreateSession(int32 NumPublicConnections = 8);
	
private:
	/** 必须跨越异步 CreateSession 调用保存，直至完成回调。 */
	TSharedPtr<FOnlineSessionSettings> LastSessionSettings;
	
	// 每项异步操作各自持有一个句柄，便于防止重复请求并精确解绑。
	FDelegateHandle CreateSessionCompleteDelegateHandle;
	
	/** 当前 World 的 Online Session 接口。World/PIE 实例变化时会刷新。 */
	IOnlineSessionPtr SessionInterface;
	
	/** 创建请求需要先销毁旧会话时，暂存新的公开连接数。 */
	int32 PendingNumPublicConnections = 8;
};
