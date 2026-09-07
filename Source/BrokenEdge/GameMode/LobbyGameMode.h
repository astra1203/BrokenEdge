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

	/** 默认地图 Lobby 在离线启动时显示菜单；监听大厅等待配置人数。 */
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
};
