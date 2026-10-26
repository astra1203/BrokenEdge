#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_StopGuardActions.generated.h"

/**
 * 瞬时任务：停止守卫当前移动，并清除 Gameplay 优先级的关注目标。
 * 放在 CanRunAi == false 分支的 Wait 之前；也可用于搜索前解除战斗关注。
 * 不改写 TargetActor（由 Service 维护），不取消已经开始的正常换弹。
 * 死亡、撤离等业务中断仍由现有战斗/生命周期组件取消换弹和执行掉落。
 */
UCLASS()
class BROKENEDGE_API UBTTask_StopGuardActions : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_StopGuardActions();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;
};
