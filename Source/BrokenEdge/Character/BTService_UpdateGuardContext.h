#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BTService_UpdateGuardContext.generated.h"

/**
 * 守卫的感知结果筛选服务。
 *
 * 将此 Service 挂在行为树顶层 Selector 上，活动期间按 Interval 读取 AI Perception 的视觉候选。
 * 视觉范围、角度和普通遮挡由感知组件判断；这里补充生命状态、烟雾和目标优先级规则。
 * 只将最终目标写入黑板，移动、关注目标、开枪、换弹等由后续 Task 实现。
 * 原 AExtractionGuard::Tick 中的自主行为已经删除，不会与此 Service 重复执行。
 *
 * 同时维护 CanRunAi、最后可见位置/时间、可配置追击窗口和首次发现/切换目标的反应时间。
 * 丢失视野时 LastKnownLocation 改为基于可见速度预判的导航追击点，之后冻结到重新发现目标。
 * NextFireTime 由交战 Task 在成功发射后更新；PatrolLocation 由巡逻 Task 写入。
 * 其余键名集中在 GuardBlackboardKeys.h，与 BB_ExtractionGuard 保持一致。
 * Service 模板可能由多个守卫共用，因此不在节点成员中保存当前目标等运行状态。
 */
UCLASS()
class BROKENEDGE_API UBTService_UpdateGuardContext : public UBTService
{
	GENERATED_BODY()

public:
	/** 设置编辑器显示名称、更新间隔和黑板键的类型限制。 */
	UBTService_UpdateGuardContext();

protected:
	/**
	 * 行为树调度的周期更新，不是角色 Actor 的 Tick。
	 * OwnerComp 用于取得本次执行所属的 AI Controller 和黑板；NodeMemory 是该 AI 的节点内存。
	 * 当前实现只使用局部变量和该 AI 的黑板，因此不需要额外分配 NodeMemory。
	 */
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	/**
	 * 要写入的目标黑板键，默认名称为 TargetActor，可在行为树中选中此 Service 后重新选择。
	 * 在 Blackboard 资产中创建 Object 类型键，Base Class 设置为 Actor 或 BrokenEdgeCharacter。
	 * 发现目标时写入角色对象；没有可见目标或当前不能感知时清空。
	 * 交战分支可用该键的 Is Set 条件，并设置 Observer Aborts 来响应目标变化。
	 */
	UPROPERTY(EditAnywhere, Category="Blackboard")
	FBlackboardKeySelector TargetActorKey;

};
