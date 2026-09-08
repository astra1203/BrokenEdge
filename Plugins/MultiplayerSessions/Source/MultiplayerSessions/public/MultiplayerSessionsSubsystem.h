#pragma once

#include "CoreMinimal.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MultiplayerSessionsSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMultiplayerOnCreateSessionComplete, bool, bWasSuccessful);
DECLARE_MULTICAST_DELEGATE_TwoParams(FMultiplayerOnFindSessionsComplete, const TArray<FOnlineSessionSearchResult>&, bool);
DECLARE_MULTICAST_DELEGATE_OneParam(FMultiplayerOnJoinSessionComplete, EOnJoinSessionCompleteResult::Type);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMultiplayerOnDestorySessionComplete, bool, bWasSuccessful);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMultiplayerOnStartSessionComplete, bool, bWasSuccessful);

/** 按 GameInstance/World 隔离的异步会话生命周期。保留既有 Destory 拼写以兼容调用方。 */
UCLASS()
class MULTIPLAYERSESSIONS_API UMultiplayerSessionsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UMultiplayerSessionsSubsystem();
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Multiplayer Sessions")
	void CreateSession(int32 NumPublicConnections, FString MatchType);
	void FindSessions(int32 MaxSearchResults);
	void JoinSession(const FOnlineSessionSearchResult& SessionResult);
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Sessions")
	void DestorySession();
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Sessions")
	void StartSession();

	bool IsBusy() const;
	bool HasSession() const;
	bool GetResolvedConnectString(FString& Address) const;
	/** 在专服 World 已开始运行后读取配置并注册会话；失败由大厅定时重试。 */
	void InitializeDedicatedServer();
	int32 GetMinPlayersToStart() const { return MinPlayersToStart; }
	bool ShouldAutoStartSession() const { return bAutoStartSession; }

	UPROPERTY(BlueprintAssignable, Category = "Multiplayer Sessions")
	FMultiplayerOnCreateSessionComplete MultiplayerOnCreateSessionComplete;
	FMultiplayerOnFindSessionsComplete MultiplayerOnFindSessionsComplete;
	FMultiplayerOnJoinSessionComplete MultiplayerOnJoinSessionComplete;
	UPROPERTY(BlueprintAssignable, Category = "Multiplayer Sessions")
	FMultiplayerOnDestorySessionComplete MultiplayerOnDestorySessionComplete;
	UPROPERTY(BlueprintAssignable, Category = "Multiplayer Sessions")
	FMultiplayerOnStartSessionComplete MultiplayerOnStartSessionComplete;

	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer Sessions")
	int32 DesiredNumPublicConnections = 8;
	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer Sessions")
	FString DesiredMatchType = TEXT("Teams");

private:
	bool RefreshSessionInterface();
	bool IsDedicatedServerInstance() const;
	bool TryGetLocalPlayerId(FUniqueNetIdRepl& OutId) const;
	void ClearOnlineDelegates();
	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void OnFindSessionsComplete(bool bWasSuccessful);
	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void OnDestorySessionComplete(FName SessionName, bool bWasSuccessful);
	void OnStartSessionComplete(FName SessionName, bool bWasSuccessful);

	IOnlineSessionPtr SessionInterface;
	TSharedPtr<FOnlineSessionSettings> LastSessionSettings;
	TSharedPtr<FOnlineSessionSearch> LastSessionSearch;
	FOnCreateSessionCompleteDelegate CreateSessionCompleteDelegate;
	FDelegateHandle CreateSessionCompleteDelegateHandle;
	FOnFindSessionsCompleteDelegate FindSessionsCompleteDelegate;
	FDelegateHandle FindSessionsCompleteDelegateHandle;
	FOnJoinSessionCompleteDelegate JoinSessionCompleteDelegate;
	FDelegateHandle JoinSessionCompleteDelegateHandle;
	FOnDestroySessionCompleteDelegate DestorySessionCompleteDelegate;
	FDelegateHandle DestorySessionCompleteDelegateHandle;
	FOnStartSessionCompleteDelegate StartSessionCompleteDelegate;
	FDelegateHandle StartSessionCompleteDelegateHandle;
	bool bCreateSessionOnDestory = false;
	int32 LastNumPublicConnections = 8;
	FString LastMatchType;
	int32 MinPlayersToStart = 2;
	bool bAutoStartSession = true;
};