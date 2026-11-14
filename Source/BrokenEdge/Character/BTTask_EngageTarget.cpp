#include "BTTask_EngageTarget.h"

#include "GuardBlackboardKeys.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Extraction/Combat/ExtractionAbilities.h"
#include "BrokenEdge/Extraction/Combat/ExtractionCombatComponent.h"
#include "BrokenEdge/Extraction/AI/ExtractionGuard.h"
#include "BrokenEdge/Extraction/Health/ExtractionHealth.h"
#include "BrokenEdge/Extraction/Combat/ExtractionSmoke.h"
#include "BrokenEdge/Extraction/Core/ExtractionState.h"
#include "BrokenEdge/Weapon/Weapon.h"

UBTTask_EngageTarget::UBTTask_EngageTarget()
{
	NodeName = TEXT("Engage Guard Target");
	bCreateNodeInstance = true;
	bNotifyTick = true;
	bNotifyTaskFinished = true;
	BlackboardKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_EngageTarget, BlackboardKey),
	                              AActor::StaticClass());
	BlackboardKey.SelectedKeyName = GuardBlackboard::TargetActor;
}

EBTNodeResult::Type UBTTask_EngageTarget::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// 不在任务重进时重置黑板上的射击/反应时间；这些值由每名 NPC 自己的黑板保存。
	FocusedTarget.Reset();
	if (auto* Guard = Cast<AExtractionGuard>(OwnerComp.GetAIOwner()))
	{
		Guard->bTakingCover = false;
		Guard->NextTacticAt = 0;
	}
	if (UpdateEngagement(OwnerComp)) return EBTNodeResult::InProgress;
	ReleaseFocus(OwnerComp);
	return EBTNodeResult::Failed;
}

void UBTTask_EngageTarget::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickTask(OwnerComp, NodeMemory, DeltaSeconds);
	if (!UpdateEngagement(OwnerComp))
	{
		ReleaseFocus(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}
}

bool UBTTask_EngageTarget::UpdateEngagement(UBehaviorTreeComponent& OwnerComp)
{
	auto* Guard = Cast<AExtractionGuard>(OwnerComp.GetAIOwner());
	auto* BB = OwnerComp.GetBlackboardComponent();
	auto* C = IsValid(Guard) ? Cast<ABrokenEdgeCharacter>(Guard->GetPawn()) : nullptr;
	if (!IsValid(Guard) || !Guard->HasAuthority() || !IsValid(C) || !C->bExtractionGuard
		|| C->IsDefeated() || !BB) return false;

	// 缺键或类型不匹配时拒绝执行，避免把默认 0 当作时间而立即开枪。
	if (!BB->IsKeyOfType<UBlackboardKeyType_Object>(BlackboardKey.SelectedKeyName)
		|| !BB->IsKeyOfType<UBlackboardKeyType_Bool>(GuardBlackboard::CanRunAI)
		|| !BB->IsKeyOfType<UBlackboardKeyType_Float>(GuardBlackboard::ReactionReadyTime)
		|| !BB->IsKeyOfType<UBlackboardKeyType_Float>(GuardBlackboard::NextFireTime)
		|| !BB->GetValueAsBool(GuardBlackboard::CanRunAI)) return false;

	auto* Target = Cast<ABrokenEdgeCharacter>(BB->GetValueAsObject(BlackboardKey.SelectedKeyName));
	const auto* PS = IsValid(Target) ? Target->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!IsValid(Target) || Target == C || Target->bExtractionGuard || Target->IsDefeated()) return false;
	// 与现有 Service 一致：普通测试地图的玩家也能成为关注目标，不强制要求搜打撤 PlayerState。
	// 有搜打撤状态时继续过滤撤离/最终阵亡；真正开枪仍受下方 Combat->CanAct() 约束。
	if (IsValid(PS))
	{
		if (PS->Life != EExtractionLife::Alive && PS->Life != EExtractionLife::Downed) return false;
	}
	else if (!Target->IsPlayerControlled()) return false;

	Guard->State = EExtractionGuardState::Engage;
	if (FocusedTarget.Get() != Target)
	{
		FocusedTarget = Target;
		Guard->SetFocus(Target);
	}
	const double Now = Guard->GetWorld()->GetTimeSeconds();
	UpdateMovement(Guard, C, Target, Now);
	// 和玩家自动射击一样按武器间隔调度。每帧只检查截止时间，未到期时不做射线检测；
	// 不再叠加固定 0.1 秒轮询，避免快射武器被轮询节奏限制成低射速。
	const float ReactionAt = BB->GetValueAsFloat(GuardBlackboard::ReactionReadyTime);
	const float FireAt = BB->GetValueAsFloat(GuardBlackboard::NextFireTime);
	if (!FMath::IsFinite(ReactionAt) || !FMath::IsFinite(FireAt)) return false;
	// 黑板时间是 float，世界时钟是 double；微小舍入误差不应多等待一整个渲染帧。
	if (Now < ReactionAt || Now + .0001 < FireAt) return true;

	auto* Combat = C->GetExtractionCombat();
	auto* Weapon = C->GetEquippedWeapon();
	const auto* SharedCombat = C->GetCombat().Get();
	if (!IsValid(Combat) || !IsValid(Weapon) || !SharedCombat) return false;
	// 普通地图的服务端守卫可开枪；正式搜打撤仍需 Active，对局结束、死亡和动作互斥仍会拦截。
	if (!Combat->CanAct() || C->GetCombatState() != ECombatState::ECS_Unoccuiped) return true;
	// Service 与 Sight 是周期更新。真正开枪前再次检查墙壁与烟雾，缩小结果滞后的窗口。
	if (!Guard->LineOfSightTo(Target)
		|| AExtractionSmoke::Blocks(Guard->GetWorld(), C->GetPawnViewLocation(), Target->GetPawnViewLocation()))
		return true;

	if (Weapon->IsEmpty())
	{
		// 无备用弹药时保持关注并等待，避免周期调用被拒绝的换弹并发送无用反馈。
		// 开始换弹后由组件自身的计时器完成；目标丢失不会凭空退回已经消耗的弹药。
		if (SharedCombat->GetCarriedAmmo() > 0) Combat->BeginServerReload();
		return true;
	}

	const uint32 Before = Combat->GetAcceptedShots();
	const FVector Aim = Target->GetActorLocation() + FVector(0, 0, 10) + FMath::VRand() * 20.f;
	Combat->FireGuard(Aim);
	const uint32 Fired = Combat->GetAcceptedShots() - Before;
	Guard->Shots += Fired;
	if (Fired)
	{
		// 只有实际被接受的射击才推进 AI 间隔；武器自己的 FireDelay 校验仍然有效。
		const float Interval = FMath::Max(Weapon->FireDelay,
			FMath::Max(0.f, GetDefault<UExtractionRules>()->GuardShotInterval));
		BB->SetValueAsFloat(GuardBlackboard::NextFireTime, static_cast<float>(Now + Interval));
	}
	return true;
}

void UBTTask_EngageTarget::UpdateMovement(AExtractionGuard* Guard, ABrokenEdgeCharacter* Self,
                                          ABrokenEdgeCharacter* Target, double Now)
{
	const auto* Rules = GetDefault<UExtractionRules>();
	auto* Weapon = Self->GetEquippedWeapon();
	const auto* Health = Self->GetExtractionHealth();
	const auto* SharedCombat = Self->GetCombat().Get();
	// 空弹匣且还有备弹才算待换弹；没有备弹时不躲，避免永远留在掩体里。
	const bool bReloading = Self->GetCombatState() == ECombatState::ECS_Reloading
		|| (IsValid(Weapon) && Weapon->IsEmpty() && SharedCombat && SharedCombat->GetCarriedAmmo() > 0);
	const bool bHit = Health && Health->LastBulletAt >= 0 && Now - Health->LastBulletAt < 1.0;
	FVector Point;
	// 换弹或刚中弹时找掩体；冷却内不重复找，避免连续中弹时来回跑。
	if (!Guard->bTakingCover && (bReloading || bHit) && Now >= Guard->NextCoverAt
		&& Guard->FindTacticalPoint(Target, true, Point))
	{
		Guard->MoveToLocation(Point, 30.f, false, true, false, true);
		Guard->bTakingCover = true;
		Guard->CoverUntil = Now + 2.0;
		Guard->NextCoverAt = Now + Rules->GuardCoverCooldown;
		return;
	}
	// 至少躲 2 秒且换弹完成后离开掩体，立即横移到能看见目标的位置。
	if (Guard->bTakingCover)
	{
		if (bReloading || Now < Guard->CoverUntil) return;
		Guard->bTakingCover = false;
		Guard->NextTacticAt = 0;
	}
	if (Rules->GuardStrafeInterval <= 0)
	{
		// 关闭横移时保持原先站定射击规则。
		Guard->StopMovement();
		return;
	}
	if (Now < Guard->NextTacticAt) return;
	Guard->NextTacticAt = Now + Rules->GuardStrafeInterval * FMath::FRandRange(.7f, 1.3f);
	if (Guard->FindTacticalPoint(Target, false, Point)) Guard->MoveToLocation(Point, 30.f, false, true, false, true);
	else Guard->StopMovement();
}

void UBTTask_EngageTarget::ReleaseFocus(UBehaviorTreeComponent& OwnerComp)
{
	if (auto* Guard = Cast<AExtractionGuard>(OwnerComp.GetAIOwner()); IsValid(Guard) && Guard->HasAuthority())
	{
		// 只清理本任务关注的对象，避免清除其他逻辑刚写入的新 Gameplay Focus。
		if (Guard->GetFocusActorForPriority(EAIFocusPriority::Gameplay) == FocusedTarget.Get())
			Guard->ClearFocus(EAIFocusPriority::Gameplay);
	}
	FocusedTarget.Reset();
}

EBTNodeResult::Type UBTTask_EngageTarget::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ReleaseFocus(OwnerComp);
	// 未注册外部异步回调，因此可以同步完成中断，不需要 FinishLatentAbort。
	// 不清 TargetActor、不重置 NextFireTime，也不取消失去视野后仍可正常完成的换弹。
	return EBTNodeResult::Aborted;
}

void UBTTask_EngageTarget::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
	                                    EBTNodeResult::Type TaskResult)
{
	ReleaseFocus(OwnerComp);
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

FString UBTTask_EngageTarget::GetStaticDescription() const
{
	return FString::Printf(TEXT("Target: %s\nUse ReactionReadyTime / NextFireTime; fire at weapon FireDelay.\n")
	                       TEXT("Target Is Set decorator: Observer Aborts = Both. Reload may finish after abort."),
	                       *BlackboardKey.SelectedKeyName.ToString());
}
