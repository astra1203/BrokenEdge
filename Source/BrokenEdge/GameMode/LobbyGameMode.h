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
	virtual void PostLogin(APlayerController* NewPlayer) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 客户端离线入口显示加入菜单；专服人数与模式由启动配置决定。 */
	UPROPERTY(EditDefaultsOnly, Category = "Sessions")
	TSubclassOf<class UMenu> MenuClass;
	UPROPERTY(EditDefaultsOnly, Category = "Sessions", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MenuNumPublicConnections = 8;
	UPROPERTY(EditDefaultsOnly, Category = "Sessions")
	FString MenuMatchType = TEXT("FreeForAll");

private:
	void TryStartGame();
	UFUNCTION()
	void OnStartSession(bool bWasSuccessful);
	UPROPERTY(Transient)
	class UMultiplayerSessionsSubsystem* Sessions = nullptr;
	FTimerHandle ReadyCheckTimer;
	FString PendingMap;
	bool bStartingSession = false;
	bool bTravelRequested = false;
	double NextSessionRetryTime = 0.0;
};
