#include "BTService_UpdateGuardContext.h"
#include "GuardBlackboardKeys.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BrokenEdgeCharacter.h"
#include "BrokenEdge/Extraction/ExtractionGuard.h"
#include "BrokenEdge/Extraction/ExtractionAbilities.h"
#include "BrokenEdge/Extraction/ExtractionSmoke.h"
#include "BrokenEdge/Extraction/ExtractionState.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"

UBTService_UpdateGuardContext::UBTService_UpdateGuardContext()
{
	// 行为树编辑器中的节点名称，方便从树上识别服务的用途。
	NodeName = TEXT("Update Guard Context");
	// 允许行为树调度 TickNode；这不会重新启用已经删除的 Controller 自定义 Tick。
	bNotifyTick = true;
	// 每 0.1 秒重新筛选感知候选，及时响应烟雾、死亡和目标距离变化。
	// 这不是视觉系统自身的检测间隔，也不代表每次更新都开枪。
	// 随机偏差为 0，初学调试时更容易观察规律；这些参数也可在节点详情中调整。
	Interval = .1f;
	RandomDeviation = 0.f;
	// 编辑器只允许选择兼容 Actor 对象的黑板键，避免误选 Bool、Vector 等类型。
	TargetActorKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateGuardContext, TargetActorKey),
	                               AActor::StaticClass());
	// 这里只给出默认键名，不会自动创建黑板资产或黑板键，仍需在编辑器中配置。
	TargetActorKey.SelectedKeyName = GuardBlackboard::TargetActor;
}

void UBTService_UpdateGuardContext::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	// 父类负责 Service 的下一次更新调度，保留此调用。
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	// 从执行组件取得当前 NPC 的 Controller 和黑板，不把它们缓存到共享节点成员中。
	auto* Guard = Cast<AExtractionGuard>(OwnerComp.GetAIOwner());
	auto* Blackboard = OwnerComp.GetBlackboardComponent();
	// 只允许服务端执行感知；没有正确的守卫控制器或黑板时，本轮不更新。
	if (!Guard || !Guard->HasAuthority() || !Blackboard) return;

	// 黑板键需要由编辑器配置。本服务的时间和状态键使用公共固定名称，目标键仍可在详情中选择。
	// 校验类型后再读写，避免缺键时静默读取默认值而跳过反应延迟或误启动巡逻。
	const bool bSchemaValid = Blackboard->IsKeyOfType<UBlackboardKeyType_Object>(TargetActorKey.SelectedKeyName)
		&& Blackboard->IsKeyOfType<UBlackboardKeyType_Bool>(GuardBlackboard::CanRunAI)
		&& Blackboard->IsKeyOfType<UBlackboardKeyType_Vector>(GuardBlackboard::LastKnownLocation)
		&& Blackboard->IsKeyOfType<UBlackboardKeyType_Bool>(GuardBlackboard::HasLastKnownLocation)
		&& Blackboard->IsKeyOfType<UBlackboardKeyType_Float>(GuardBlackboard::LastSeenTime)
		&& Blackboard->IsKeyOfType<UBlackboardKeyType_Bool>(GuardBlackboard::SearchActive)
		&& Blackboard->IsKeyOfType<UBlackboardKeyType_Float>(GuardBlackboard::ReactionReadyTime)
		&& Blackboard->IsKeyOfType<UBlackboardKeyType_Float>(GuardBlackboard::NextFireTime)
		&& Blackboard->IsKeyOfType<UBlackboardKeyType_Bool>(GuardBlackboard::ReturningToRoute);
	if (!bSchemaValid)
	{
		Guard->Target = nullptr;
		if (Blackboard->IsKeyOfType<UBlackboardKeyType_Object>(TargetActorKey.SelectedKeyName))
			Blackboard->ClearValue(TargetActorKey.SelectedKeyName);
		if (Blackboard->IsKeyOfType<UBlackboardKeyType_Bool>(GuardBlackboard::CanRunAI))
			Blackboard->SetValueAsBool(GuardBlackboard::CanRunAI, false);
		return;
	}

	// 感知不再依赖搜打撤对局阶段、特定 GameMode 或持久化存储。
	// 只要求观察者存活、世界和感知组件有效；不满足时清理旧目标。
	const auto* C = Cast<ABrokenEdgeCharacter>(Guard->GetPawn());
	const auto* World = Guard->GetWorld();
	// 从当前守卫的控制器读取组件；缺少组件时同样走清空目标的路径，避免残留旧目标。
	const auto* Perception = Guard->GetPerceptionComponent();
	// 沿用当前 Service 的独立感知设计，不在这里强制依赖 Extraction GameMode 或存储。
	// 禁用角色行为/失去 Pawn/死亡时关闭行为分支；射击仍由战斗组件校验对局状态。
	const bool bCanRun = IsValid(C) && C->bExtractionGuard && !C->IsDefeated()
		&& !C->bDisableGameplay && World && IsValid(Perception);
	// Seen 仅表示“这一轮实际看见的最佳目标”，每次检查都重新计算。
	ABrokenEdgeCharacter* Seen = nullptr;
	if (bCanRun)
	{
		// 只取“当前视觉感知到”的对象，不取历史记忆，也不把听觉结果当作可见目标。
		// 距离、视角和普通墙壁遮挡已由 Sight 处理，无需再遍历 PlayerArray 调用 Sees。
		TArray<AActor*> Candidates;
		Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Candidates);
		float BestDistanceSquared = TNumericLimits<float>::Max();
		bool bBestDowned = true;
		for (AActor* Actor : Candidates)
		{
			// 只接受由玩家控制的角色，排除自身和其他敌人；普通测试地图不要求搜打撤 PlayerState。
			auto* Other = IsValid(Actor) ? Cast<ABrokenEdgeCharacter>(Actor) : nullptr;
			if (!IsValid(Other) || Other == C || Other->bExtractionGuard || Other->IsDefeated()
				|| !Other->IsPlayerControlled()) continue;
			const auto* OtherPS = Other->GetPlayerState<AExtractionPlayerState>();
			// 搜打撤中继续过滤已撤离/最终阵亡玩家；普通 GameMode 中无此状态则按存活处理。
			if (OtherPS && OtherPS->Life != EExtractionLife::Alive && OtherPS->Life != EExtractionLife::Downed) continue;

			// 当前烟雾没有碰撞，Sight 可能仍将烟雾后的玩家列为可见候选。
			// 每轮补充检查，才能在烟雾出现时丢失目标、消散后重新选择目标。
			if (!Guard->LineOfSightTo(Other)
				|| AExtractionSmoke::Blocks(World, C->GetPawnViewLocation(), Other->GetPawnViewLocation())) continue;

			// 诊断计数只统计通过所有筛选的候选，不是发现玩家所需的累计门槛。
			++Guard->VisibleChecks;
			const bool bDowned = OtherPS && OtherPS->Life == EExtractionLife::Downed;
			const float DistanceSquared = FVector::DistSquared(C->GetActorLocation(), Other->GetActorLocation());
			// 先比较是否倒地，再比较距离：即使后续调大 SightRadius，仍保证未倒地优先。
			if (!Seen || (bBestDowned && !bDowned)
				|| (bBestDowned == bDowned && DistanceSquared < BestDistanceSquared))
			{
				BestDistanceSquared = DistanceSquared;
				bBestDowned = bDowned;
				Seen = Other;
			}
		}
	}

	// 先读取上次结果，随后统一更新黑板；目标从 A 变为 B 也需要新的反应时间。
	const auto* PreviousTarget = Blackboard->GetValueAsObject(TargetActorKey.SelectedKeyName);
	const bool bWasSearching = Blackboard->GetValueAsBool(GuardBlackboard::SearchActive);
	const double Now = World ? World->GetTimeSeconds() : 0;
	// 暂缓通知，避免 TargetActor 变化触发 Decorator 时，其他时间键还没写完。
	Blackboard->PauseObserverNotifications();
	Blackboard->SetValueAsBool(GuardBlackboard::CanRunAI, bCanRun);
	if (Seen)
	{
		// 更新可见时的运动快照；目标丢失后不再读取其位置和速度。
		Guard->LastVisibleTargetVelocity = Seen->GetVelocity();
		if (PreviousTarget != Seen)
		{
			const float Reaction = FMath::Max(0.f, GetDefault<UExtractionRules>()->GuardReactionSeconds);
			Blackboard->SetValueAsFloat(GuardBlackboard::ReactionReadyTime, static_cast<float>(Now + Reaction));
		}
		Blackboard->SetValueAsVector(GuardBlackboard::LastKnownLocation, Seen->GetActorLocation());
		Blackboard->SetValueAsBool(GuardBlackboard::HasLastKnownLocation, true);
		Blackboard->SetValueAsFloat(GuardBlackboard::LastSeenTime, static_cast<float>(Now));
		Blackboard->SetValueAsBool(GuardBlackboard::SearchActive, false);
		Blackboard->SetValueAsBool(GuardBlackboard::ReturningToRoute, false);
	}
	else
	{
		// 只延续一次丢失目标产生的搜索，不能因历史位置仍存在而在巡逻时不断重开搜索。
		const float LastSeen = Blackboard->GetValueAsFloat(GuardBlackboard::LastSeenTime);
		bool bSearching = bCanRun && (PreviousTarget || bWasSearching)
			&& Blackboard->GetValueAsBool(GuardBlackboard::HasLastKnownLocation)
			&& FMath::IsFinite(LastSeen) && Now - LastSeen >= 0
			&& Now - LastSeen < FMath::Max(0.f, GetDefault<UExtractionRules>()->GuardPursuitSeconds);
		if (bSearching && PreviousTarget)
		{
			// 在清空 TargetActor / 通知分支切换之前写好追击点。现有搜索 Move To 仍使用
			// LastKnownLocation，无需新增黑板键。只计算一次，避免预判点随每轮更新漂移。
			FVector Destination;
			bSearching = Guard->BuildPursuitDestination(
				Blackboard->GetValueAsVector(GuardBlackboard::LastKnownLocation), Destination);
			if (bSearching) Blackboard->SetValueAsVector(GuardBlackboard::LastKnownLocation, Destination);
		}
		Blackboard->SetValueAsBool(GuardBlackboard::SearchActive, bSearching);
		if (!bCanRun) Blackboard->SetValueAsBool(GuardBlackboard::ReturningToRoute, false);
		else if ((PreviousTarget || bWasSearching) && !bSearching)
			Blackboard->SetValueAsBool(GuardBlackboard::ReturningToRoute, true);
		// 只计算追击目标与状态；实际路径跟随/朝向仍由内置 Move To 负责。
		if (!IsValid(C) || C->IsDefeated()) Guard->State = EExtractionGuardState::Dead;
		else if (bSearching) Guard->State = EExtractionGuardState::Search;
		else if (PreviousTarget || bWasSearching) Guard->State = EExtractionGuardState::Return;
	}

	// 保留 Controller.Target 作为现有测试和诊断的输出镜像；原 Tick 已删除，它不再驱动行为。
	Guard->Target = Seen;
	// 按配置的键名更新当前 NPC 的黑板，供行为树 Decorator 和 Task 读取。
	// 无目标时一定要清空，否则行为树会继续把上一轮目标当作当前可见目标。
	// 这里不调用 SetFocus、MoveToLocation 或 FireGuard，所有动作留给你后续制作的行为树。
	if (Seen) Blackboard->SetValueAsObject(TargetActorKey.SelectedKeyName, Seen);
	else Blackboard->ClearValue(TargetActorKey.SelectedKeyName);
	Blackboard->ResumeObserverNotifications(true);
}
