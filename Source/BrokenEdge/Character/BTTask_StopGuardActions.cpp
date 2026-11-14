#include "BTTask_StopGuardActions.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BrokenEdgeCharacter.h"
#include "BrokenEdge/Extraction/AI/ExtractionGuard.h"

UBTTask_StopGuardActions::UBTTask_StopGuardActions()
{
	NodeName = TEXT("Stop Guard Actions");
}

EBTNodeResult::Type UBTTask_StopGuardActions::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	auto* Guard = Cast<AExtractionGuard>(OwnerComp.GetAIOwner());
	if (!IsValid(Guard) || !Guard->HasAuthority()) return EBTNodeResult::Failed;

	// 即便 Pawn 已经死亡/解除控制，也要允许停止控制器残留的寻路和关注。
	Guard->StopMovement();
	Guard->ClearFocus(EAIFocusPriority::Gameplay);
	if (const auto* C = Cast<ABrokenEdgeCharacter>(Guard->GetPawn()); !IsValid(C) || C->IsDefeated())
		Guard->State = EExtractionGuardState::Dead;

	// 没有定时器、异步操作或内部运行数据，因此立即成功，无需 TickTask/AbortTask。
	return EBTNodeResult::Succeeded;
}

FString UBTTask_StopGuardActions::GetStaticDescription() const
{
	return TEXT("Stop movement and clear Gameplay focus. Does not clear perception or cancel reload.");
}
