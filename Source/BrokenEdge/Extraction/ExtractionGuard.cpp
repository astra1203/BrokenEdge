#include "ExtractionGuard.h"
#include "ExtractionGameMode.h"
#include "ExtractionState.h"
#include "ExtractionAbilities.h"
#include "ExtractionSmoke.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionCombatComponent.h"
#include "ExtractionInteractionComponent.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "NavigationSystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
// 守卫使用服务器 AIController 的轻量状态机，不依赖玩家 PlayerState，也不在此使用行为树。
// 已有 Tick 注释说明巡逻、交战、丢失目标搜索及返回的具体顺序；开火仍通过撤离战斗组件校验。

// 提供编辑器可配置的世界坐标路线容器，路线本身不执行寻路。
AExtractionPatrolRoute::AExtractionPatrolRoute()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Route")));
}

// 以 0.1 秒间隔决策，不为 AI 创建玩家状态。
AExtractionGuard::AExtractionGuard()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = .1f;
	bWantsPlayerState = false;
}

// 先检查目标生命、30 米距离与前向视锥，再检查烟雾和场景视线，不能隔烟锁定玩家。
bool AExtractionGuard::Sees(const ABrokenEdgeCharacter* Other) const
{
	const auto* C = Cast<ABrokenEdgeCharacter>(GetPawn());
	const auto* PS = Other ? Other->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!C || !PS || Other->IsDefeated() || (PS->Life != EExtractionLife::Alive && PS->Life != EExtractionLife::Downed))
		return false;
	const FVector Delta = Other->GetActorLocation() - C->GetActorLocation();
	if (Delta.SizeSquared() > FMath::Square(3000.f) || FVector::DotProduct(
		C->GetActorForwardVector(), Delta.GetSafeNormal()) < .5f) return false;
	if (AExtractionSmoke::Blocks(GetWorld(), C->GetPawnViewLocation(), Other->GetPawnViewLocation())) return false;
	return LineOfSightTo(Other);
}

// 每轮先尝试发现玩家，有目标则交战；失去目标后搜索最后位置，超时回到巡逻路线。
void AExtractionGuard::Tick(float Delta)
{
	Super::Tick(Delta);
	auto* C = Cast<ABrokenEdgeCharacter>(GetPawn());
	const auto* GS = GetWorld()->GetGameState<AExtractionGameState>();
	// 没有受控角色或角色已被击败时，停止移动并清除目标，结束本次 AI 更新。
	if (!C || C->IsDefeated())
	{
		StopMovement();
		ClearFocus(EAIFocusPriority::Gameplay);
		Target = nullptr;
		State = EExtractionGuardState::Dead;
		return;
	}
	// 仅在服务端对局进行中且持久化存储可用时决策；其他情况下停止移动。
	const auto* GM = GetWorld()->GetAuthGameMode<AExtractionGameMode>();
	if (!GS || GS->Phase != EExtractionPhase::Active || !GM || !GM->Store() || !GM->Store()->IsAvailable())
	{
		StopMovement();
		return;
	}
	// 所有反应、射击和移动间隔均使用当前世界时间计算。
	const double Now = GetWorld()->GetTimeSeconds();
	// Seen 是本轮选出的可见目标；评分越小，攻击优先级越高。
	ABrokenEdgeCharacter* Seen = nullptr;
	float Best = TNumericLimits<float>::Max();
	// 从玩家列表取得角色，再由 Sees 检查生命状态、距离、视角、烟雾和场景遮挡。
	for (APlayerState* PS : GS->PlayerArray)
		if (auto* Other = Cast<ABrokenEdgeCharacter>(PS->GetPawn()); Sees(Other))
		{
			// 仅统计可见检查通过次数，不要求累计达到某个次数才发现玩家。
			++VisibleChecks;
			// 倒地惩罚高于当前 30 米视野内的最大距离平方：未倒地优先，同类目标选最近的。
			const float Score = FVector::DistSquared(C->GetActorLocation(), Other->GetActorLocation())
				+ (Other->GetExtractionAbilities()->IsDowned() ? 10000000.f : 0.f);
			if (Score < Best)
			{
				Seen = Other;
				Best = Score;
			}
		}
	if (Seen)
	{
		const auto* Rules = GetDefault<UExtractionRules>();
		// 首次发现、重新发现或切换目标时才重置反应时间，持续看见同一目标不会延后开枪。
		if (Target != Seen) ReactAt = Now + FMath::Max(0.f, Rules->GuardReactionSeconds);
		Target = Seen;
		// 持续记录目标位置和最后看见的时间；LostAt 并非首次丢失目标的时间。
		LastKnown = Seen->GetActorLocation();
		LostAt = Now;
		// 交战时关注目标并站定；反应时间控制开枪时机，不延迟进入交战状态。
		State = EExtractionGuardState::Engage;
		SetFocus(Seen);
		StopMovement();
		// 同时满足反应时间和 AI 射击间隔后，才尝试换弹或开枪。
		if (auto* W = C->GetEquippedWeapon(); W && Now >= ReactAt && Now >= NextFireAt)
		{
			if (W->IsEmpty()) C->GetExtractionCombat()->BeginServerReload();
			else
			{
				// 服务端战斗组件会继续校验射速、弹药和战斗状态，调用不一定成功发射。
				const uint32 Before = C->GetExtractionCombat()->GetAcceptedShots();
				// 在目标位置上移 10 cm，并添加长度为 20 cm 的随机方向偏移作为瞄准误差。
				C->GetExtractionCombat()->FireGuard(LastKnown + FVector(0, 0, 10) + FMath::VRand() * 20);
				// 只统计实际被接受的射击，成功后才推进 AI 的下次开火时间。
				const uint32 Fired = C->GetExtractionCombat()->GetAcceptedShots() - Before;
				Shots += Fired;
				if (Fired) NextFireAt = Now + FMath::Max(.1f, Rules->GuardShotInterval);
			}
		}
		// 有可见目标时，本轮不再执行后面的搜索和巡逻逻辑。
		return;
	}
	// 本轮没有可见目标、上一轮仍有目标：解除关注，进入搜索并允许立即请求移动。
	if (Target)
	{
		Target = nullptr;
		ClearFocus(EAIFocusPriority::Gameplay);
		State = EExtractionGuardState::Search;
		NextMove = 0;
	}
	if (State == EExtractionGuardState::Search)
	{
		// 从最后看见玩家起搜索五秒；位置固定为 LastKnown，不追踪失去视野后的实际位置。
		if (Now - LostAt < 5)
		{
			if (Now >= NextMove)
			{
				// 最多每秒提交一次寻路请求，指定到达容差为 80 cm。
				MoveToLocation(LastKnown, 80);
				NextMove = Now + 1;
			}
			return;
		}
		// 搜索超时后返回尚未完成的巡逻目标点；每轮仍会先检查能否重新发现玩家。
		State = EExtractionGuardState::Return;
		NextMove = 0;
	}
	// 返回和巡逻共用路线移动逻辑，没有有效路线时不再提交移动请求。
	if (!Route || Route->Points.IsEmpty()) return;
	const FVector Point = Route->Points[PatrolIndex % Route->Points.Num()];
	// 水平距离小于 110 cm 即记为到点，循环切换下一点并恢复巡逻状态。
	if (FVector::DistSquared2D(C->GetActorLocation(), Point) < FMath::Square(110.f))
	{
		++PatrolArrivals;
		PatrolIndex = (PatrolIndex + 1) % Route->Points.Num();
		State = EExtractionGuardState::Patrol;
		// 延后 0.6 秒再允许提交移动请求；这里不会主动停止当前移动。
		NextMove = Now + .6;
	}
	if (Now >= NextMove)
	{
		// 返回或巡逻途中最多每秒提交一次寻路请求，指定到达容差为 70 cm。
		MoveToLocation(Point, 70);
		NextMove = Now + 1;
	}
}

// 服务器按配置数量选择已烘焙路线，投影到导航网格后延迟生成角色、标记守卫并初始化库存。
void AExtractionGameMode::SpawnGuards()
{
	int32 Count = FMath::Clamp(GetDefault<UExtractionRules>()->Guards, 0, 4);
	// Explicit server scenario configuration; clients cannot change this through a URL or RPC.
	FParse::Value(FCommandLine::Get(), TEXT("ExtractionGuards="), Count);
	Count = FMath::Clamp(Count, 0, 4);
	TArray<AExtractionPatrolRoute*> Routes;
	for (TActorIterator<AExtractionPatrolRoute> It(GetWorld()); It; ++It) if (!It->Points.IsEmpty()) Routes.Add(*It);
	Routes.Sort([](const auto& A, const auto& B) { return A.GetName() < B.GetName(); });
	if (Count && Routes.IsEmpty()) UE_LOG(LogTemp, Error, TEXT("EXTRACTION_GUARDS missing cooked patrol routes"));
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	for (int32 I = 0; I < Count && !Routes.IsEmpty(); ++I)
	{
		auto* Route = Routes[I % Routes.Num()];
		const int32 Index = (I / Routes.Num()) % Route->Points.Num();
		FNavLocation Projected;
		if (!Nav || !Nav->ProjectPointToNavigation(Route->Points[Index], Projected, FVector(200, 200, 300)))
		{
			UE_LOG(LogTemp, Error, TEXT("EXTRACTION_GUARDS route lacks navigation"));
			continue;
		}
		const FTransform Transform(FRotator::ZeroRotator, Projected.Location + FVector(0, 0, 100));
		auto* C = GetWorld()->SpawnActorDeferred<ABrokenEdgeCharacter>(DefaultPawnClass, Transform, nullptr, nullptr,
		                                                               ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!C) continue;
		C->bExtractionGuard = true;
		C->ExtractionGuardId = I;
		C->FinishSpawning(Transform);
		C->SetHealth(100);
		C->SetShield(0);
		C->GetCharacterMovement()->MaxWalkSpeed = 220;

		C->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		auto* AI = GetWorld()->SpawnActor<AExtractionGuard>();
		AI->Route = Route;
		AI->PatrolIndex = Index;
		AI->Possess(C);
		C->GuardInventory->InitializeGuard();
		C->ForceNetUpdate();
		UE_LOG(LogTemp, Display, TEXT("EXTRACTION_GUARD_SPAWN Actor=%s Route=%s"), *C->GetName(), *Route->GetName());
	}
}

// 结束对局时清空守卫库存再销毁角色与 AI，同时移除烟雾，避免清场产生额外掉落。
void AExtractionGameMode::RemoveGuards()
{
	for (TActorIterator<AExtractionGuard> It(GetWorld()); It; ++It)
	{
		if (auto* C = Cast<ABrokenEdgeCharacter>(It->GetPawn()))
		{
			C->GuardInventory->ClearSettled();
			C->Destroy();
		}
		It->Destroy();
	}
	for (TActorIterator<AExtractionSmoke> It(GetWorld()); It; ++It) It->Destroy();
}
