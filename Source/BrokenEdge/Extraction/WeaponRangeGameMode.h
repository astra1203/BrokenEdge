#pragma once
#include "ExtractionGameMode.h"
#include "WeaponRangeGameMode.generated.h"
// 一次性本地训练模式，复用撤离武器和背包规则，但跳过正式出战票据、结算与奖励流程。
// 不要把训练场能开枪等同于正式撤离完整闭环通过；持久化层另为训练地图选择隔离目录。

/** Disposable local training mode. No raid tickets, rewards or settlement. */
UCLASS()
class BROKENEDGE_API AWeaponRangeGameMode : public AExtractionGameMode
{
	GENERATED_BODY()

public:
	// 指定训练地图定义并强制单人预期人数，其余初始化仍由撤离基类完成。
	virtual void InitGame(const FString& Map, const FString& Options, FString& Error) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* Player) override;
	// 有意不调用基类对局 Tick：持续提供 Active 测试环境并解除冻结，不激活或结算长期出战记录。
	virtual void Tick(float Delta) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
};
