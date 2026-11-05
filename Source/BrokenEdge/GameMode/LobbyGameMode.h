#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "LobbyGameMode.generated.h"

UCLASS()
class BROKENEDGE_API ALobbyGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	ALobbyGameMode();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	void SetExtractionSquad(APlayerController* Player, uint8 Squad);
	void AssignExtractionSquads();
	void SetPlayerReady(APlayerController* Player, bool bReady);
	void SetMatchType(APlayerController* Player, const FString& MatchType);
	void SetExtractionMap(APlayerController* Player, FName MapId);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 客户端离线入口显示加入菜单；专服人数与模式由启动配置决定。 */
	UPROPERTY(EditDefaultsOnly, Category = "Sessions")
	TSubclassOf<class UMenu> MenuClass;
	UPROPERTY(EditDefaultsOnly, Category = "Sessions", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MenuNumPublicConnections = 8;
	UPROPERTY(EditDefaultsOnly, Category = "Sessions")
	FString MenuMatchType = TEXT("Teams");

private:
	void TryStartGame();
	bool AreAllPlayersReady() const;
	FString ExtractionStartError() const;
	void CancelCountdown();
	void OnMatchTypeUpdated(bool bWasSuccessful);
	UFUNCTION()
	void OnStartSession(bool bWasSuccessful);
	UPROPERTY(Transient)
	class UMultiplayerSessionsSubsystem* Sessions = nullptr;
	FTimerHandle ReadyCheckTimer;
	FString PendingMap;
	bool bStartingSession = false;
	bool bTravelRequested = false;
	bool bPreparedRaidsCleared = false;
	double NextSessionRetryTime = 0.0;
};
