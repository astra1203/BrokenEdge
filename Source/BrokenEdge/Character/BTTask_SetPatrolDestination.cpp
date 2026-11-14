#include "BTTask_SetPatrolDestination.h"

#include "GuardBlackboardKeys.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BrokenEdgeCharacter.h"
#include "BrokenEdge/Extraction/AI/ExtractionGuard.h"
#include "BrokenEdge/Extraction/AI/ExtractionPatrolArea.h"
#include "Components/BoxComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"

UBTTask_SetPatrolDestination::UBTTask_SetPatrolDestination()
{
	NodeName = TEXT("Set Guard Patrol Destination");
	BlackboardKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_SetPatrolDestination, BlackboardKey));
	BlackboardKey.SelectedKeyName = GuardBlackboard::PatrolLocation;
}

EBTNodeResult::Type UBTTask_SetPatrolDestination::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	auto* Guard = Cast<AExtractionGuard>(OwnerComp.GetAIOwner());
	auto* BB = OwnerComp.GetBlackboardComponent();
	// 只有返回分支会尝试复用旧目标；失败时必须清空，避免 Move To 继续读到过期坐标。
	auto Fail = [&]()
	{
		if (BB && BB->IsKeyOfType<UBlackboardKeyType_Vector>(BlackboardKey.SelectedKeyName))
			BB->ClearValue(BlackboardKey.SelectedKeyName);
		return EBTNodeResult::Failed;
	};
	auto* C = IsValid(Guard) ? Cast<ABrokenEdgeCharacter>(Guard->GetPawn()) : nullptr;
	if (!IsValid(Guard) || !Guard->HasAuthority() || !IsValid(C) || !C->bExtractionGuard
		|| C->IsDefeated() || !BB
		|| !BB->IsKeyOfType<UBlackboardKeyType_Vector>(BlackboardKey.SelectedKeyName)
		|| !BB->IsKeyOfType<UBlackboardKeyType_Bool>(GuardBlackboard::CanRunAI)
		|| !BB->GetValueAsBool(GuardBlackboard::CanRunAI)) return Fail();

	FVector Point;
	if (Guard->bUseRandomPatrol)
	{
		if (!SelectRandomDestination(*Guard, *C, *BB, Point))
		{
			UE_LOG(LogTemp, Verbose, TEXT("EXTRACTION_GUARD_PATROL no reachable point within activity bounds Actor=%s"),
			       *C->GetName());
			return Fail();
		}
	}
	else
	{
		// 保留手工路线模式；世界坐标不能再叠加路线 Actor 的位置。
		if (!IsValid(Guard->Route) || Guard->Route->Points.IsEmpty()) return Fail();
		const int32 Count = Guard->Route->Points.Num();
		const int32 Index = ((Guard->PatrolIndex % Count) + Count) % Count;
		Point = Guard->Route->Points[Index];
		if (!FAISystem::IsValidLocation(Point)) return Fail();
		Guard->PatrolIndex = Index;
	}
	Guard->State = BB->IsKeyOfType<UBlackboardKeyType_Bool>(GuardBlackboard::ReturningToRoute)
		&& BB->GetValueAsBool(GuardBlackboard::ReturningToRoute)
		? EExtractionGuardState::Return : EExtractionGuardState::Patrol;
	BB->SetValueAsVector(BlackboardKey.SelectedKeyName, Point);
	return EBTNodeResult::Succeeded;
}

bool UBTTask_SetPatrolDestination::SelectRandomDestination(AExtractionGuard& Guard, ABrokenEdgeCharacter& Character,
	                                                     UBlackboardComponent& Blackboard, FVector& OutLocation) const
{
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Guard.GetWorld());
	if (!Nav || !FMath::IsFinite(MinimumTravelDistance) || MinimumTravelDistance <= 0) return false;
	auto* NavData = Nav->GetNavDataForProps(Character.GetNavAgentPropertiesRef(), Character.GetActorLocation());
	if (!NavData) return false; // 不运行时生成 NavMesh，关卡仍需配置 NavMeshBoundsVolume。

	auto HasAllowedPath = [&](const FVector& Goal)
	{
		if (!Guard.IsRandomPatrolLocationAllowed(Goal)) return false;
		// Reachable 随机点仍需从 NPC 的当前位置检查一次，尤其是搜索后 NPC 已离开原区域时。
		const auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
			&Guard, Character.GetNavAgentLocation(), Goal, &Character, Guard.GetDefaultNavigationFilterClass());
		return Path && Path->IsValid() && !Path->IsPartial() && Guard.IsRandomPatrolPathAllowed(Path->PathPoints);
	};

	const FVector Previous = Blackboard.GetValueAsVector(BlackboardKey.SelectedKeyName);
	if (Blackboard.IsKeyOfType<UBlackboardKeyType_Bool>(GuardBlackboard::ReturningToRoute)
		&& Blackboard.GetValueAsBool(GuardBlackboard::ReturningToRoute) && HasAllowedPath(Previous))
	{
		OutLocation = Previous;
		return true;
	}

	FNavLocation ProjectedOrigin;
	if (!IsValid(Guard.PatrolArea))
	{
		if (!FAISystem::IsValidLocation(Guard.PatrolOrigin) || !FMath::IsFinite(Guard.RandomPatrolRadius)
			|| !FMath::IsFinite(Guard.RandomPatrolHeight) || Guard.RandomPatrolRadius <= 0
			|| Guard.RandomPatrolHeight <= 0) return false;
		if (!Nav->ProjectPointToNavigation(Guard.PatrolOrigin, ProjectedOrigin,
			FVector(100, 100, Guard.RandomPatrolHeight), NavData)) return false;
	}

	for (int32 Attempt = 0; Attempt < FMath::Clamp(MaxRandomAttempts, 1, 32); ++Attempt)
	{
		FNavLocation Candidate;
		bool bFound = false;
		if (IsValid(Guard.PatrolArea))
		{
			// 活动盒内采样，再投影到实际地面；投影后再次检查边界，不能把盒外地面当作合法点。
			const float Height = Guard.PatrolArea->Bounds->Bounds.BoxExtent.Z;
			bFound = Nav->ProjectPointToNavigation(Guard.PatrolArea->SamplePatrolLocation(), Candidate,
				FVector(100, 100, FMath::Max(1.f, Height)), NavData);
		}
		else
		{
			// 圆心固定在初次接管的位置，而不是 NPC 当前坐标，避免越走越远。
			bFound = Nav->GetRandomReachablePointInRadius(ProjectedOrigin.Location, Guard.RandomPatrolRadius,
			                                             Candidate, NavData);
		}
		if (!bFound || FVector::DistSquared2D(Character.GetActorLocation(), Candidate.Location)
			< FMath::Square(MinimumTravelDistance) || !HasAllowedPath(Candidate.Location)) continue;
		OutLocation = Candidate.Location;
		return true;
	}
	return false;
}

FString UBTTask_SetPatrolDestination::GetStaticDescription() const
{
	return FString::Printf(TEXT("Write random reachable point in guard activity bounds to %s.\n")
	                       TEXT("Use Random Patrol OFF: follow Route.Points. Follow with built-in Move To."),
	                       *BlackboardKey.SelectedKeyName.ToString());
}
