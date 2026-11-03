#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTTask_EngageTarget.generated.h"

class ABrokenEdgeCharacter;

/**
 * 持续任务：关注当前可见目标，反应时间结束后按间隔尝试射击/换弹。
 * 交战中按规则横移换位，换弹或刚中弹时找可达掩体，换完弹再横移出来继续射击。
 * Blackboard Key 选择 TargetActor；交战分支添加 Is Set 装饰器并设置 Observer Aborts=Both。
 * Service 维护 ReactionReadyTime；任务仅在实际开枪后更新 NextFireTime。
 * 弹药、装填时间、射速和伤害仍由 ExtractionCombatComponent 决定。
 * 正常情况下保持 InProgress，直到分支被中断；目标失效时自行返回 Failed 作为兜底。
 */
UCLASS()
class BROKENEDGE_API UBTTask_EngageTarget : public UBTTask_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTTask_EngageTarget();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
	                            EBTNodeResult::Type TaskResult) override;

	/** 旧行为树资产兼容字段；射击现在由武器截止时间控制，此数值不再限制射速。 */
	UPROPERTY(EditAnywhere, Category="Guard|Combat", meta=(ClampMin="0.05", Units="s"))
	float CombatCheckInterval = .1f;

private:
	// 此任务开启 bCreateNodeInstance，每个 AI 的节点副本单独保存弱引用，避免四个守卫串目标。
	// 实际射击时间存黑板，跨分支中断仍保留；这里只缓存本次任务拥有的 Focus。
	TWeakObjectPtr<ABrokenEdgeCharacter> FocusedTarget;

	bool UpdateEngagement(UBehaviorTreeComponent& OwnerComp);
	// 交战走位：只用 AIController 的导航移动，Gameplay Focus 保持朝向目标。
	void UpdateMovement(class AExtractionGuard* Guard, ABrokenEdgeCharacter* Self, ABrokenEdgeCharacter* Target,
	                    double Now);
	void ReleaseFocus(UBehaviorTreeComponent& OwnerComp);
};
