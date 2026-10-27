#include "BTTask_CompletePatrolArrival.h"

#include "GuardBlackboardKeys.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BrokenEdgeCharacter.h"
#include "BrokenEdge/Extraction/ExtractionGuard.h"

UBTTask_CompletePatrolArrival::UBTTask_CompletePatrolArrival()
{
	NodeName = TEXT("Complete Guard Patrol Arrival");
	BlackboardKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_CompletePatrolArrival, BlackboardKey));
	BlackboardKey.SelectedKeyName = GuardBlackboard::PatrolLocation;
}

EBTNodeResult::Type UBTTask_CompletePatrolArrival::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	auto* Guard = Cast<AExtractionGuard>(OwnerComp.GetAIOwner());
	auto* BB = OwnerComp.GetBlackboardComponent();
	const auto* C = IsValid(Guard) ? Cast<ABrokenEdgeCharacter>(Guard->GetPawn()) : nullptr;
	if (!IsValid(Guard) || !Guard->HasAuthority() || !IsValid(C) || !C->bExtractionGuard
		|| C->IsDefeated() || !BB
		|| !BB->IsKeyOfType<UBlackboardKeyType_Bool>(GuardBlackboard::CanRunAI)
		|| !BB->GetValueAsBool(GuardBlackboard::CanRunAI)
		|| !BB->IsKeyOfType<UBlackboardKeyType_Vector>(BlackboardKey.SelectedKeyName))
		return EBTNodeResult::Failed;

	const FVector Destination = BB->GetValueAsVector(BlackboardKey.SelectedKeyName);
	// 未设置的 Vector 键使用无效位置哨兵值；不能把它当作世界原点或合法巡逻目的地。
	if (!FAISystem::IsValidLocation(Destination)) return EBTNodeResult::Failed;
	if (Guard->bUseRandomPatrol)
	{
		// 运行中缩小/替换活动范围后，旧目的地不能再被计作合法到点。
		if (!Guard->IsRandomPatrolLocationAllowed(Destination)) return EBTNodeResult::Failed;
	}
	else
	{
		if (!IsValid(Guard->Route) || !Guard->Route->Points.IsValidIndex(Guard->PatrolIndex)
			|| !Destination.Equals(Guard->Route->Points[Guard->PatrolIndex], 1.f)) return EBTNodeResult::Failed;
	}
	const FVector Location = C->GetActorLocation();
	if (!FMath::IsFinite(ArrivalRadius) || !FMath::IsFinite(VerticalTolerance)
		|| FVector::DistSquared2D(Location, Destination) > FMath::Square(FMath::Max(1.f, ArrivalRadius))
		|| FMath::Abs(Location.Z - Destination.Z) > FMath::Max(1.f, VerticalTolerance))
		return EBTNodeResult::Failed;

	++Guard->PatrolArrivals;
	if (!Guard->bUseRandomPatrol)
		Guard->PatrolIndex = (Guard->PatrolIndex + 1) % Guard->Route->Points.Num();
	Guard->State = EExtractionGuardState::Patrol;
	// 每个巡逻周期由 SetPatrolDestination 重新写入；这里只确认完成，绝不自己移动。
	BB->ClearValue(BlackboardKey.SelectedKeyName);
	if (BB->IsKeyOfType<UBlackboardKeyType_Bool>(GuardBlackboard::ReturningToRoute))
		BB->SetValueAsBool(GuardBlackboard::ReturningToRoute, false);
	return EBTNodeResult::Succeeded;
}

FString UBTTask_CompletePatrolArrival::GetStaticDescription() const
{
	return FString::Printf(TEXT("Confirm %s arrival (XY %.0f cm, Z %.0f cm), clear destination.\n")
	                       TEXT("Random patrol: choose a new point next cycle. Fixed route: advance index.\n")
	                       TEXT("Follow built-in Move To with this task, then Wait 0.6s."),
	                       *BlackboardKey.SelectedKeyName.ToString(), ArrivalRadius, VerticalTolerance);
}
