#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "ExtractionGuard.generated.h"
// 守卫使用服务器 AIController 的轻量状态机，不依赖玩家 PlayerState，也不在此使用行为树。
// 已有 Tick 注释说明巡逻、交战、丢失目标搜索及返回的具体顺序；开火仍通过撤离战斗组件校验。

class ABrokenEdgeCharacter;

UCLASS()
class BROKENEDGE_API AExtractionPatrolRoute : public AActor
{
	GENERATED_BODY()

public:
	// 提供编辑器可配置的世界坐标路线容器，路线本身不执行寻路。
	AExtractionPatrolRoute();
	/** World-space points, authored and projected to the cooked navigation mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FVector> Points;
};

UENUM()
enum class EExtractionGuardState : uint8 { Patrol, Engage, Search, Return, Dead };

UCLASS()
class BROKENEDGE_API AExtractionGuard : public AAIController
{
	GENERATED_BODY()

public:
	// 以 0.1 秒间隔决策，不为 AI 创建玩家状态。
	AExtractionGuard();
	// 每轮先尝试发现玩家，有目标则交战；失去目标后搜索最后位置，超时回到巡逻路线。
	virtual void Tick(float Delta) override;
	UPROPERTY()
	TObjectPtr<AExtractionPatrolRoute> Route;
	UPROPERTY()
	TObjectPtr<ABrokenEdgeCharacter> Target;
	EExtractionGuardState State = EExtractionGuardState::Patrol;
	int32 PatrolIndex = 0;
	// 调试计数：可见检查、成功射击和巡逻到点次数，不参与发现目标的门槛。
	int32 VisibleChecks = 0, Shots = 0, PatrolArrivals = 0;
	// 先检查目标生命、30 米距离与前向视锥，再检查烟雾和场景视线，不能隔烟锁定玩家。
	bool Sees(const ABrokenEdgeCharacter* Other) const;

private:
	// 最后可见位置用于有限时间搜索；丢失视野后不读取玩家实时位置继续追踪。
	FVector LastKnown = FVector::ZeroVector;
	// LostAt 记录最后看见目标的时间，ReactAt 是反应完成时刻，NextMove 用于限制寻路请求频率。
	double LostAt = 0, ReactAt = 0, NextMove = 0;
	double NextFireAt = 0;
};
