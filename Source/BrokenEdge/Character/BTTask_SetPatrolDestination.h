#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTTask_SetPatrolDestination.generated.h"

/**
 * 瞬时任务：默认在 Controller 的活动范围内随机选择可达点，写入 PatrolLocation。
 * 后面连接内置 Move To（Blackboard Key=PatrolLocation），本任务不提交寻路请求。
 * 关闭 Controller.Use Random Patrol 时，兼容原 Route.Points 固定巡逻。
 * 随机模式检查完整路径与范围，返回分支优先复用尚未完成的有效目的地。
 */
UCLASS()
class BROKENEDGE_API UBTTask_SetPatrolDestination : public UBTTask_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTTask_SetPatrolDestination();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	/** 过滤原地附近的小碎步。单位 cm；活动区域较小时可适当降低。 */
	UPROPERTY(EditAnywhere, Category="Guard|Random Patrol", meta=(ClampMin="1.0", Units="cm"))
	float MinimumTravelDistance = 250.f;

	/** 限制一次选点的开销。全部失败就让行为树进入末尾 Wait，下一轮再尝试。 */
	UPROPERTY(EditAnywhere, Category="Guard|Random Patrol", meta=(ClampMin="1", ClampMax="32"))
	int32 MaxRandomAttempts = 8;

private:
	bool SelectRandomDestination(class AExtractionGuard& Guard, class ABrokenEdgeCharacter& Character,
	                             class UBlackboardComponent& Blackboard, FVector& OutLocation) const;
};
