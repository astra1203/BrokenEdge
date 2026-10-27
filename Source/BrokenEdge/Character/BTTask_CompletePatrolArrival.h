#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTTask_CompletePatrolArrival.generated.h"

/**
 * 瞬时任务：确认守卫确实到达当前巡逻点，计数并清除已完成目标。
 * 放在内置 Move To 成功之后，后接内置 Wait（0.6 秒）。
 * 随机模式确认目的地仍在活动范围内；固定模式同时确认目的地对应当前路线点并推进索引。
 * 成功后清空 PatrolLocation，防止重复调用在同一点反复增加到点计数。
 */
UCLASS()
class BROKENEDGE_API UBTTask_CompletePatrolArrival : public UBTTask_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTTask_CompletePatrolArrival();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	/** 守卫中心至目标点的水平距离；应覆盖 Move To 的实际完成半径。单位 cm。 */
	UPROPERTY(EditAnywhere, Category="Guard|Patrol", meta=(ClampMin="1.0", Units="cm"))
	float ArrivalRadius = 110.f;

	/** 路线点通常在地面，角色中心高出约一个胶囊半高；同时避免隔着楼层误判到点。 */
	UPROPERTY(EditAnywhere, Category="Guard|Patrol", meta=(ClampMin="1.0", Units="cm"))
	float VerticalTolerance = 150.f;
};
