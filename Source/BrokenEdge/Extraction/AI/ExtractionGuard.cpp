#include "BrokenEdge/Extraction/AI/ExtractionGuard.h"
#include "BrokenEdge/Extraction/Map/ExtractionMapDefinition.h"
#include "BrokenEdge/Extraction/Core/ExtractionGameMode.h"
#include "BrokenEdge/Extraction/Combat/ExtractionAbilities.h"
#include "BrokenEdge/Extraction/Combat/ExtractionSmoke.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/AI/ExtractionPatrolArea.h"
#include "BrokenEdge/Character/GuardBlackboardKeys.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "UObject/ConstructorHelpers.h"

AExtractionPatrolRoute::AExtractionPatrolRoute()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Route")));
}

bool AExtractionGuard::BuildPursuitDestination(const FVector& LastSeenLocation, FVector& OutDestination) const
{
	const auto* GuardCharacter = Cast<ABrokenEdgeCharacter>(GetPawn());
	const auto* Rules = GetDefault<UExtractionRules>();
	if (!GuardCharacter || !FAISystem::IsValidLocation(LastSeenLocation)) return false;
	const float Budget = FMath::Max(0.f, Rules->GuardPursuitMaxDistance);
	if (FVector::Dist(GuardCharacter->GetActorLocation(), LastSeenLocation) > Budget) return false;
	OutDestination = LastSeenLocation;
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	// 缺少导航时保留原有的最后位置搜索语义。实际移动仍由内置 Move To 判断失败，
	// 不用直线移动绕过导航/碰撞；生产地图必须铺设 NavMeshBoundsVolume。
	if (!Nav || !Nav->GetNavDataForProps(GuardCharacter->GetNavAgentPropertiesRef(), GuardCharacter->GetActorLocation()))
		return true;

	// 这里只使用最后可见时缓存的平面速度；不沿用跳跃速度，也不持有隐藏玩家的引用。
	FVector Offset = LastVisibleTargetVelocity * FMath::Max(0.f, Rules->GuardPursuitPredictionSeconds);
	Offset.Z = 0;
	Offset = Offset.GetClampedToMaxSize(FMath::Max(0.f, Rules->GuardPursuitPredictionDistance));
	auto TryGoal = [&](const FVector& Candidate)
	{
		FNavLocation Projected;
		if (!Nav->ProjectPointToNavigation(Candidate, Projected, FVector(150, 150, 250),
			&GuardCharacter->GetNavAgentPropertiesRef())) return false;
		const auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
			GetWorld(), GuardCharacter->GetNavAgentLocation(), Projected.Location, GetPawn(), GetDefaultNavigationFilterClass());
		if (!Path || !Path->IsValid() || Path->IsPartial() || Path->GetPathLength() > Budget) return false;
		OutDestination = Projected.Location;
		return true;
	};
	// 预判点在墙内、断开的平台或需绕很远时退回真实最后位置；两者都不可达则放弃。
	return TryGoal(LastSeenLocation + Offset) || TryGoal(LastSeenLocation);
}

bool AExtractionGuard::FindTacticalPoint(const APawn* Threat, bool bCover, FVector& Out) const
{
	auto* Me = Cast<ABrokenEdgeCharacter>(GetPawn());
	const auto* Rules = GetDefault<UExtractionRules>();
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const float Radius = bCover ? Rules->GuardCoverRadius : Rules->GuardStrafeDistance;
	if (!Me || !IsValid(Threat) || !Nav || Radius <= 0) return false;
	// 导航点在地面上，眼高按胶囊半高加眼睛偏移换算。
	const FVector From = Me->GetNavAgentLocation();
	const FVector ThreatEye = Threat->GetPawnViewLocation();
	const float EyeHeight = Me->GetSimpleCollisionHalfHeight() + Me->BaseEyeHeight;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ExtractionGuardTactics), false, Me);
	Query.AddIgnoredActor(Threat);
	if (Me->GetEquippedWeapon()) Query.AddIgnoredActor(Me->GetEquippedWeapon());
	// 掩体要求站姿眼睛和胸口都被挡住；横移点要求眼睛能看到目标，换位后可以继续射击。
	auto Exposed = [&](const FVector& Ground, float Height)
	{
		FHitResult Hit;
		return !GetWorld()->LineTraceSingleByChannel(Hit, ThreatEye, Ground + FVector(0, 0, Height), ECC_Visibility,
		                                              Query);
	};
	TArray<FVector> Candidates;
	if (bCover)
	{
		for (int32 I = 0; I < 16; ++I)
		{
			FNavLocation Point;
			if (Nav->GetRandomReachablePointInRadius(From, Radius, Point)) Candidates.Add(Point.Location);
		}
		// 先试近处，减少暴露在开阔地的时间。
		Candidates.Sort([&](const FVector& A, const FVector& B)
		{
			return FVector::DistSquared(A, From) < FVector::DistSquared(B, From);
		});
	}
	else
	{
		const FVector Side = FVector::CrossProduct((ThreatEye - From).GetSafeNormal2D(), FVector::UpVector);
		const float Sign = FMath::RandBool() ? 1.f : -1.f;
		for (const float Scale : {1.f, .6f})
			for (const float Direction : {Sign, -Sign})
				Candidates.Add(From + Side * Direction * Radius * Scale);
	}
	for (const FVector& Candidate : Candidates)
	{
		FNavLocation Projected;
		if (!Nav->ProjectPointToNavigation(Candidate, Projected, FVector(100, 100, 200), &Me->GetNavAgentPropertiesRef()))
			continue;
		const FVector Ground = Projected.Location;
		if (FMath::Abs(Ground.Z - From.Z) > 200) continue;
		if (bCover ? Exposed(Ground, EyeHeight) || Exposed(Ground, EyeHeight * .6f) : !Exposed(Ground, EyeHeight))
			continue;
		const auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
			GetWorld(), Me->GetNavAgentLocation(), Ground, GetPawn(), GetDefaultNavigationFilterClass());
		if (!Path || !Path->IsValid() || Path->IsPartial() || Path->GetPathLength() > Radius * 1.5f) continue;
		Out = Ground;
		return true;
	}
	return false;
}

AExtractionGuard::AExtractionGuard()
{
	// 守卫不占玩家席位；不再设置自定义 TickInterval 或运行旧 AI 状态机。
	bWantsPlayerState = false;
	// 路径跟随提供移动方向 Focus；交战 Task 的 Gameplay Focus 会以更高优先级覆盖它。
	bAllowStrafe = false;

	// 仅建立资产引用，实际运行在角色和装备就绪之后；硬引用也让打包收集此树及其依赖。
	static ConstructorHelpers::FObjectFinder<UBehaviorTree> DefaultTree(
		TEXT("/Game/Blueprints/Ai/BT_ExtractionGuard.BT_ExtractionGuard"));
	if (DefaultTree.Succeeded()) GuardBehaviorTree = DefaultTree.Object;

	// 感知组件负责收集视觉候选；仅添加感知能力，不会启动行为树或执行任何战斗动作。
	auto* Perception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("GuardPerception"));
	SetPerceptionComponent(*Perception);
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("GuardSightConfig"));
	// 该属性是半视角：左右各 60 度，总视野约 120 度。
	SightConfig->PeripheralVisionAngleDegrees = 60.f;
	// 目前未使用感知系统的队伍编号，玩家可能被归类为 Neutral。
	// 先接收全部阵营，随后由 Service 排除自身、其他守卫和非玩家对象。
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;
	ApplySightRules();
	Perception->SetDominantSense(SightConfig->GetSenseImplementation());
}

void AExtractionGuard::HearShot(UWorld* World, const FVector& Location, float Radius)
{
	if (!World || Radius <= 0) return;
	for (TActorIterator<AExtractionGuard> It(World); It; ++It)
		if (const APawn* Pawn = It->GetPawn(); Pawn && FVector::DistSquared(Pawn->GetActorLocation(), Location) <= Radius * Radius)
			It->Hear(Location);
}

void AExtractionGuard::Hear(const FVector& Location)
{
	auto* Board = GetBlackboardComponent();
	const auto* C = Cast<ABrokenEdgeCharacter>(GetPawn());
	if (!Board || !C || C->IsDefeated() || Target || !Board->GetValueAsBool(GuardBlackboard::CanRunAI)) return;
	// 枪声位置投影到导航网格，投不上就原样交给搜索移动。
	FVector Destination = Location;
	FNavLocation Projected;
	if (const auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		Nav && Nav->ProjectPointToNavigation(Location, Projected, FVector(500, 500, 300)))
		Destination = Projected.Location;
	// 复用丢失目标后的搜索分支：写好最后已知位置并标记搜索中，感知 Service 会按追击时长自然结束搜索。
	Board->PauseObserverNotifications();
	Board->SetValueAsVector(GuardBlackboard::LastKnownLocation, Destination);
	Board->SetValueAsBool(GuardBlackboard::HasLastKnownLocation, true);
	Board->SetValueAsFloat(GuardBlackboard::LastSeenTime, static_cast<float>(GetWorld()->GetTimeSeconds()));
	Board->SetValueAsBool(GuardBlackboard::SearchActive, true);
	Board->SetValueAsBool(GuardBlackboard::ReturningToRoute, false);
	Board->ResumeObserverNotifications(true);
	State = EExtractionGuardState::Search;
}

void AExtractionGuard::ApplySightRules()
{
	if (!SightConfig || !GetPerceptionComponent()) return;
	const auto* Rules = GetDefault<UExtractionRules>();
	SightConfig->SightRadius = FMath::IsFinite(Rules->GuardSightRadius)
		? FMath::Max(0.f, Rules->GuardSightRadius) : 6000.f;
	SightConfig->LoseSightRadius = FMath::Max(SightConfig->SightRadius,
		FMath::IsFinite(Rules->GuardLoseSightRadius) ? Rules->GuardLoseSightRadius : SightConfig->SightRadius);
	// 接管时重新应用半径；视角、阵营过滤等仍使用控制器现有配置。
	// ConfigureSense 会在组件已注册时请求刷新监听器，使实际 Sight 查询使用新的距离。
	GetPerceptionComponent()->ConfigureSense(*SightConfig);
}

bool AExtractionGuard::StartGuardAI()
{
	// AI 逻辑仅在服务器运行，不要求搜打撤 GameMode、Active 阶段或持久化存储。
	auto* C = Cast<ABrokenEdgeCharacter>(GetPawn());
	if (!HasAuthority() || !IsValid(C) || C->IsDefeated())
	{
		UE_LOG(LogTemp, Warning, TEXT("EXTRACTION_GUARD_BT invalid authority or pawn Controller=%s"), *GetName());
		return false;
	}
	// 地图放置和 SpawnActor 都可能在 PostInitializeComponents 中触发 Possess。
	// 此时角色的 Combat->Character 等引用尚未就绪，必须等 BeginPlay 后再初始化装备。
	if (!C->HasActorBegunPlay())
	{
		GetWorldTimerManager().ClearTimer(StartGuardAITimer);
		StartGuardAITimer = GetWorldTimerManager().SetTimerForNextTick(this, &AExtractionGuard::InitializeGuardAI);
		return false;
	}
	GetWorldTimerManager().ClearTimer(StartGuardAITimer);
	// 只要由此类型控制器接管，就作为敌人初始化；不要求关卡或 GameMode 额外打标。
	ApplySightRules();
	C->bExtractionGuard = true;
	if (!C->GuardInventory->GetContents().Id.IsValid())
	{
		C->ApplyVitalsChange({100 - C->GetHealth(), -C->GetShield(), EExtractionVitalsReason::Restore});
	}
	C->GetCharacterMovement()->MaxWalkSpeed = FMath::Max(0.f, GetDefault<UExtractionRules>()->GuardWalkSpeed);
	C->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	// InitializeGuard 内部有一次性初始化保护，重新接管不会重置背包、补满弹药。
	C->GuardInventory->InitializeGuard();
	C->ConfigureGuardRotation();
	C->ForceNetUpdate();
	// 没有武器也允许运行巡逻/关注等非战斗分支，射击任务自行检查装备。
	if (!IsValid(GuardBehaviorTree) || !GuardBehaviorTree->BlackboardAsset)
	{
		UE_LOG(LogTemp, Error, TEXT("EXTRACTION_GUARD_BT missing behavior tree or blackboard Controller=%s"), *GetName());
		return false;
	}
	if (const auto* BT = Cast<UBehaviorTreeComponent>(GetBrainComponent());
		BT && BT->IsRunning() && BT->GetRootTree() == GuardBehaviorTree) return true;

	// UE 会创建/初始化关联的 BlackboardComponent，并以循环模式启动 BehaviorTreeComponent。
	// 无需再手动创建这两个组件，也无需在角色 Tick 中调用行为树。
	const bool bStarted = RunBehaviorTree(GuardBehaviorTree);
	if (bStarted)
	{
		UE_LOG(LogTemp, Display, TEXT("EXTRACTION_GUARD_BT started Actor=%s Tree=%s"),
		       *C->GetName(), *GuardBehaviorTree->GetPathName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("EXTRACTION_GUARD_BT failed Actor=%s Tree=%s"),
		       *C->GetName(), *GuardBehaviorTree->GetPathName());
	}
	return bStarted;
}

void AExtractionGuard::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (!HasAuthority() || !Cast<ABrokenEdgeCharacter>(GetPawn())) return;
	ApplySightRules();
	auto* GuardCharacter = CastChecked<ABrokenEdgeCharacter>(GetPawn());
	GuardCharacter->bExtractionGuard = true;
	GuardCharacter->ConfigureGuardRotation();
	// 在任何巡逻/搜索移动之前记录中心；重复启动行为树不会把中心改成 NPC 当前的位置。
	if (PatrolOriginPawn.Get() != InPawn)
	{
		PatrolOriginPawn = InPawn;
		PatrolOrigin = InPawn->GetActorLocation();
	}
	// 自动覆盖地图放置、运行时生成和重新接管；不再依赖 SpawnGuards 主动调用启动入口。
	GetWorldTimerManager().ClearTimer(StartGuardAITimer);
	StartGuardAITimer = GetWorldTimerManager().SetTimerForNextTick(this, &AExtractionGuard::InitializeGuardAI);
}

void AExtractionGuard::InitializeGuardAI()
{
	StartGuardAI();
}

void AExtractionGuard::StopGuardAI()
{
	GetWorldTimerManager().ClearTimer(StartGuardAITimer);
	// 生命周期结束不能等待某个潜伏任务自行结束；强制中止，重新接管时才能可靠启动新一轮。
	if (auto* BT = Cast<UBehaviorTreeComponent>(GetBrainComponent())) BT->StopTree(EBTStopMode::Forced);
	else if (auto* Brain = GetBrainComponent()) Brain->StopLogic(TEXT("Guard stopped"));
	StopMovement();
	ClearFocus(EAIFocusPriority::Gameplay);
	Target = nullptr;
	LastVisibleTargetVelocity = FVector::ZeroVector;
}

void AExtractionGuard::OnUnPossess()
{
	StopGuardAI();
	Super::OnUnPossess();
}

void AExtractionGuard::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopGuardAI();
	Super::EndPlay(EndPlayReason);
}

bool AExtractionGuard::IsRandomPatrolLocationAllowed(const FVector& Location) const
{
	if (!FAISystem::IsValidLocation(Location)) return false;
	const auto* C = Cast<ABrokenEdgeCharacter>(GetPawn());
	const float Inset = IsValid(C) ? C->GetCapsuleComponent()->GetScaledCapsuleRadius() : 0.f;
	if (IsValid(PatrolArea))
		return PatrolArea->GetWorld() == GetWorld() && PatrolArea->ContainsPatrolLocation(Location, Inset);
	if (!FAISystem::IsValidLocation(PatrolOrigin) || !FMath::IsFinite(RandomPatrolRadius)
		|| !FMath::IsFinite(RandomPatrolHeight) || RandomPatrolRadius <= Inset || RandomPatrolHeight <= 0)
		return false;
	return FVector::DistSquared2D(Location, PatrolOrigin) <= FMath::Square(RandomPatrolRadius - Inset)
		&& FMath::Abs(Location.Z - PatrolOrigin.Z) <= RandomPatrolHeight;
}

bool AExtractionGuard::IsRandomPatrolPathAllowed(const TArray<FVector>& PathPoints) const
{
	if (PathPoints.Num() < 2 || !IsRandomPatrolLocationAllowed(PathPoints.Last())) return false;
	bool bEnteredArea = false;
	for (const FVector& Point : PathPoints)
	{
		if (!FAISystem::IsValidLocation(Point)) return false;
		const bool bInside = IsRandomPatrolLocationAllowed(Point);
		if (bEnteredArea && !bInside) return false;
		bEnteredArea |= bInside;
	}
	// 圆柱和活动盒都是凸区域：内部路径拐点之间的直线段也在区域中。
	// 从外部返回允许存在外部前缀；这并不限制交战/搜索分支自身的移动范围。
	return bEnteredArea;
}

void AExtractionGameMode::SpawnGuards()
{
	// 7F 起每个区域一条巡逻路线（核心区 4 条 + 新区域 4 条），上限随之放开到 8。
	constexpr int32 MaxGuards = 8;
	int32 Count = FMath::Clamp(LoadedMapDefinition && LoadedMapDefinition->GuardCount >= 0
		? LoadedMapDefinition->GuardCount : GetDefault<UExtractionRules>()->Guards, 0, MaxGuards);
	// Explicit server scenario configuration; clients cannot change this through a URL or RPC.
	FParse::Value(FCommandLine::Get(), TEXT("ExtractionGuards="), Count);
	Count = FMath::Clamp(Count, 0, MaxGuards);
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
		C->ApplyVitalsChange({100 - C->GetHealth(), -C->GetShield(), EExtractionVitalsReason::Restore});
		C->GetCharacterMovement()->MaxWalkSpeed = FMath::Max(0.f, GetDefault<UExtractionRules>()->GuardWalkSpeed);

		C->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		// FinishSpawning 已可能按角色配置自动生成控制器；必须复用，避免生成两套 AI。
		auto* AI = Cast<AExtractionGuard>(C->GetController());
		if (!AI) AI = GetWorld()->SpawnActor<AExtractionGuard>();
		if (!AI)
		{
			UE_LOG(LogTemp, Error, TEXT("EXTRACTION_GUARDS failed to spawn controller Actor=%s"), *C->GetName());
			C->Destroy();
			continue;
		}
		AI->Route = Route;
		AI->PatrolIndex = Index;
		if (AI->GetPawn() != C) AI->Possess(C);
		// 显式调用与 OnPossess 的自动启动共用幂等入口；路线在此之前已经分配。
		AI->StartGuardAI();
		C->ForceNetUpdate();
		UE_LOG(LogTemp, Display, TEXT("EXTRACTION_GUARD_SPAWN Actor=%s Route=%s"), *C->GetName(), *Route->GetName());
	}
}

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
