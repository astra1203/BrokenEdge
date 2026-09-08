#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "LobbyGameState.generated.h"

/** 大厅设置和倒计时由服务器复制；每个玩家的准备状态保存在 PlayerState。 */
UCLASS()
class BROKENEDGE_API ALobbyGameState : public AGameState
{
    GENERATED_BODY()
public:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lobby")
    FString RoomMatchType;
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lobby")
    int32 MinimumPlayers = 2;
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lobby")
    int32 Capacity = 8;
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lobby")
    double CountdownEndTime = 0.0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lobby")
    bool bTravelling = false;
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lobby")
    FString LobbyStatus;
};
