#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BrokenEdge/Character/BTTask_CompletePatrolArrival.h"
#include "BrokenEdge/Character/BTTask_EngageTarget.h"
#include "BrokenEdge/Character/BTTask_SetPatrolDestination.h"
#include "BrokenEdge/Character/BTTask_StopGuardActions.h"
#include "BrokenEdge/Character/BTService_UpdateGuardContext.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Character/GuardBlackboardKeys.h"
#include "BrokenEdge/Extraction/ExtractionGuard.h"
#include "BrokenEdge/Extraction/ExtractionState.h"
#include "BrokenEdge/Extraction/ExtractionPatrolArea.h"
#include "BrokenEdge/Extraction/ExtractionCombatComponent.h"
#include "BrokenEdge/Extraction/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/ExtractionAbilities.h"
#include "BrokenEdge/Extraction/ExtractionGuardAim.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Sight.h"
#include "TimerManager.h"
#if WITH_EDITOR
#include "ActorFactories/ActorFactory.h"
#include "Builders/CubeBuilder.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#endif

namespace
{
	/** 使用临时 World/黑板，不修改地图、实际黑板资产或用户的持久化存档。 */
	struct FGuardTaskFixture
	{
		UWorld* World = nullptr;
		UBlackboardData* Data = nullptr;
		UBehaviorTree* Tree = nullptr;
		AExtractionGuard* Guard = nullptr;
		ABrokenEdgeCharacter* Pawn = nullptr;
		UBlackboardComponent* BB = nullptr;
		UBehaviorTreeComponent* BT = nullptr;

		FGuardTaskFixture(bool bCreateNavigation = false)
		{
			const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
				.CreateNavigation(bCreateNavigation).CreateAISystem(true).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(bCreateNavigation ? EWorldType::Editor : EWorldType::Game,
			                            false, NAME_None, nullptr, true,
			                            ERHIFeatureLevel::Num, &Init);
			if (!World) return;
			Data = NewObject<UBlackboardData>(World);
			AddKey<UBlackboardKeyType_Bool>(GuardBlackboard::CanRunAI);
			AddKey<UBlackboardKeyType_Bool>(GuardBlackboard::ReturningToRoute);
			AddKey<UBlackboardKeyType_Vector>(GuardBlackboard::PatrolLocation);
			AddKey<UBlackboardKeyType_Object>(GuardBlackboard::TargetActor);
			AddKey<UBlackboardKeyType_Float>(GuardBlackboard::ReactionReadyTime);
			AddKey<UBlackboardKeyType_Float>(GuardBlackboard::NextFireTime);
			AddKey<UBlackboardKeyType_Vector>(GuardBlackboard::LastKnownLocation);
			AddKey<UBlackboardKeyType_Bool>(GuardBlackboard::HasLastKnownLocation);
			AddKey<UBlackboardKeyType_Float>(GuardBlackboard::LastSeenTime);
			AddKey<UBlackboardKeyType_Bool>(GuardBlackboard::SearchActive);
			Tree = NewObject<UBehaviorTree>(World);
			Tree->BlackboardAsset = Data;
			Guard = World->SpawnActor<AExtractionGuard>();
			Pawn = World->SpawnActor<ABrokenEdgeCharacter>();
			if (!Guard || !Pawn) return;
			// 原有测试继续验证固定路线；随机巡逻测试在各自用例中显式开启。
			Guard->bUseRandomPatrol = false;
			Pawn->bExtractionGuard = true;
			Guard->Possess(Pawn);
			if (!Guard->UseBlackboard(Data, BB)) return;
			BT = NewObject<UBehaviorTreeComponent>(Guard);
			BT->RegisterComponent();
			BT->CacheBlackboardComponent(BB);
			BB->SetValueAsBool(GuardBlackboard::CanRunAI, true);
			Guard->Route = World->SpawnActor<AExtractionPatrolRoute>();
			Guard->Route->Points = {FVector(1000, 0, 0), FVector(2000, 0, 0)};
		}

		~FGuardTaskFixture()
		{
			if (World) World->DestroyWorld(false);
		}

		bool IsReady() const { return Guard && Pawn && BB && BT && Guard->Route; }

		template<typename T> void AddKey(FName Name)
		{
			FBlackboardEntry Entry;
			Entry.EntryName = Name;
			Entry.KeyType = NewObject<T>(Data);
			Data->Keys.Add(Entry);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardPatrolTaskTest, "BrokenEdge.Extraction.GuardBT.PatrolArrival",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuardPatrolTaskTest::RunTest(const FString&)
{
	FGuardTaskFixture F;
	if (!TestTrue(TEXT("Transient AI fixture initialized"), F.IsReady())) return false;
	auto* Set = NewObject<UBTTask_SetPatrolDestination>(F.Tree);
	auto* Complete = NewObject<UBTTask_CompletePatrolArrival>(F.Tree);
	Set->InitializeFromAsset(*F.Tree);
	Complete->InitializeFromAsset(*F.Tree);
	TestEqual(TEXT("Set destination succeeds"), Set->ExecuteTask(*F.BT, nullptr), EBTNodeResult::Succeeded);
	TestEqual(TEXT("World-space point copied without actor offset"),
		F.BB->GetValueAsVector(GuardBlackboard::PatrolLocation), F.Guard->Route->Points[0]);
	TestEqual(TEXT("Far away does not advance patrol"), Complete->ExecuteTask(*F.BT, nullptr), EBTNodeResult::Failed);
	TestEqual(TEXT("Rejected arrival does not count"), F.Guard->PatrolArrivals, 0);
	F.Pawn->SetActorLocation(FVector(1000, 0, 500));
	TestEqual(TEXT("Another floor cannot count as arrival"), Complete->ExecuteTask(*F.BT, nullptr), EBTNodeResult::Failed);
	F.Pawn->SetActorLocation(FVector(1000, 0, 90));
	F.BB->SetValueAsBool(GuardBlackboard::ReturningToRoute, true);
	TestEqual(TEXT("Ground point allows capsule height"), Complete->ExecuteTask(*F.BT, nullptr), EBTNodeResult::Succeeded);
	TestEqual(TEXT("Successful arrival advances once"), F.Guard->PatrolIndex, 1);
	TestEqual(TEXT("Successful arrival counted once"), F.Guard->PatrolArrivals, 1);
	TestFalse(TEXT("Return flag cleared"), F.BB->GetValueAsBool(GuardBlackboard::ReturningToRoute));
	TestEqual(TEXT("Duplicate completion rejected"), Complete->ExecuteTask(*F.BT, nullptr), EBTNodeResult::Failed);
	TestEqual(TEXT("Duplicate cannot count twice"), F.Guard->PatrolArrivals, 1);
	Set->ExecuteTask(*F.BT, nullptr);
	F.Pawn->SetActorLocation(FVector(2000, 0, 90));
	TestEqual(TEXT("Last point can complete"), Complete->ExecuteTask(*F.BT, nullptr), EBTNodeResult::Succeeded);
	TestEqual(TEXT("Route wraps to first point"), F.Guard->PatrolIndex, 0);
	// 动态换路线后，不应把旧黑板坐标当成新路线的有效到达。
	Set->ExecuteTask(*F.BT, nullptr);
	F.Guard->Route->Points[0] = FVector(3000, 0, 0);
	TestEqual(TEXT("Stale destination rejected"), Complete->ExecuteTask(*F.BT, nullptr), EBTNodeResult::Failed);
	F.Guard->Route->Points.Reset();
	TestEqual(TEXT("Empty route fails safely"), Set->ExecuteTask(*F.BT, nullptr), EBTNodeResult::Failed);
	TestFalse(TEXT("Empty route removes stale destination"),
		FAISystem::IsValidLocation(F.BB->GetValueAsVector(GuardBlackboard::PatrolLocation)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardTaskIsolationTest, "BrokenEdge.Extraction.GuardBT.IsolationAndInvalidTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuardTaskIsolationTest::RunTest(const FString&)
{
	FGuardTaskFixture A, B;
	if (!TestTrue(TEXT("Two independent AI fixtures initialized"), A.IsReady() && B.IsReady())) return false;
	// 故意让两个 AI 执行同一个瞬时任务模板，验证任务没有把运行数据保存在共享成员中。
	auto* Set = NewObject<UBTTask_SetPatrolDestination>(A.Tree);
	Set->InitializeFromAsset(*A.Tree);
	A.Guard->PatrolIndex = -1;
	B.Guard->Route->Points[0] = FVector(8000, 0, 0);
	Set->ExecuteTask(*A.BT, nullptr);
	Set->ExecuteTask(*B.BT, nullptr);
	TestEqual(TEXT("Negative index normalized"), A.Guard->PatrolIndex, 1);
	TestEqual(TEXT("Second guard keeps its index"), B.Guard->PatrolIndex, 0);
	TestEqual(TEXT("First guard destination independent"),
		A.BB->GetValueAsVector(GuardBlackboard::PatrolLocation), FVector(2000, 0, 0));
	TestEqual(TEXT("Second guard destination independent"),
		B.BB->GetValueAsVector(GuardBlackboard::PatrolLocation), FVector(8000, 0, 0));

	auto* Engage = NewObject<UBTTask_EngageTarget>(A.Tree);
	Engage->InitializeFromAsset(*A.Tree);
	TestEqual(TEXT("Missing target cannot start engagement"), Engage->ExecuteTask(*A.BT, nullptr), EBTNodeResult::Failed);
	A.BB->SetValueAsObject(GuardBlackboard::TargetActor, A.Pawn);
	TestEqual(TEXT("Self target rejected"), Engage->ExecuteTask(*A.BT, nullptr), EBTNodeResult::Failed);
	TestEqual(TEXT("Invalid targets cannot add shots"), A.Guard->Shots, 0);
	A.Guard->SetFocus(B.Pawn);
	auto* Stop = NewObject<UBTTask_StopGuardActions>();
	TestEqual(TEXT("Stop task completes immediately"), Stop->ExecuteTask(*A.BT, nullptr), EBTNodeResult::Succeeded);
	TestNull(TEXT("Stop task clears Gameplay focus"), A.Guard->GetFocusActorForPriority(EAIFocusPriority::Gameplay));
	TestEqual(TEXT("Stop task preserves perception output"),
		A.BB->GetValueAsObject(GuardBlackboard::TargetActor), static_cast<UObject*>(A.Pawn));
	A.BB->SetValueAsBool(GuardBlackboard::CanRunAI, false);
	TestEqual(TEXT("Disabled AI cannot begin patrol"), Set->ExecuteTask(*A.BT, nullptr), EBTNodeResult::Failed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardEngagementAbortTest, "BrokenEdge.Extraction.GuardBT.EngagementAbort",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuardEngagementAbortTest::RunTest(const FString&)
{
	FGuardTaskFixture F;
	if (!TestTrue(TEXT("AI fixture initialized"), F.IsReady())) return false;
	auto* Human = F.World->SpawnActor<ABrokenEdgeCharacter>(FVector(500, 0, 90), FRotator::ZeroRotator);
	auto* PS = F.World->SpawnActor<AExtractionPlayerState>();
	if (!Human || !PS) return false;
	PS->Life = EExtractionLife::Alive;
	Human->SetPlayerState(PS);
	F.BB->SetValueAsObject(GuardBlackboard::TargetActor, Human);
	F.BB->SetValueAsFloat(GuardBlackboard::ReactionReadyTime, 100.f);
	F.BB->SetValueAsFloat(GuardBlackboard::NextFireTime, 200.f);
	auto* Engage = NewObject<UBTTask_EngageTarget>(F.Tree);
	Engage->InitializeFromAsset(*F.Tree);
	TestTrue(TEXT("Production engagement node uses per-AI instances"), Engage->HasInstance());
	// 此处已手动创建一个独立 Task 对象。关闭引擎的二次实例查找，以直接测试其生命周期包装函数。
	// 正式行为树依然使用构造函数中的 bCreateNodeInstance=true，不会使用这个测试对象。
	Engage->ForceInstancing(false);
	TestEqual(TEXT("Valid target starts latent engagement"), Engage->ExecuteTask(*F.BT, nullptr), EBTNodeResult::InProgress);
	TestEqual(TEXT("Target becomes Gameplay focus"),
		F.Guard->GetFocusActorForPriority(EAIFocusPriority::Gameplay), static_cast<AActor*>(Human));
	TestEqual(TEXT("Reaction delay prevents early shot"), F.Guard->Shots, 0);
	TestEqual(TEXT("Abort finishes synchronously"), Engage->WrappedAbortTask(*F.BT, nullptr), EBTNodeResult::Aborted);
	TestNull(TEXT("Abort releases focus"), F.Guard->GetFocusActorForPriority(EAIFocusPriority::Gameplay));
	TestEqual(TEXT("Abort preserves fire deadline"), F.BB->GetValueAsFloat(GuardBlackboard::NextFireTime), 200.f);
	TestEqual(TEXT("Abort preserves service target"), F.BB->GetValueAsObject(GuardBlackboard::TargetActor),
		static_cast<UObject*>(Human));
	TestEqual(TEXT("Task can reenter"), Engage->ExecuteTask(*F.BT, nullptr), EBTNodeResult::InProgress);
	TestEqual(TEXT("Reenter does not restart reaction clock"),
		F.BB->GetValueAsFloat(GuardBlackboard::ReactionReadyTime), 100.f);
	Engage->WrappedOnTaskFinished(*F.BT, nullptr, EBTNodeResult::Failed);
	TestNull(TEXT("Normal finish also releases focus"), F.Guard->GetFocusActorForPriority(EAIFocusPriority::Gameplay));
	auto* PC = F.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Generic player controller spawned"), PC)) return false;
	PC->Possess(Human);
	// UE 5.8 的 IsPlayerControlled 依赖普通 PlayerState；这里替换为普通类型，而非清空全部状态。
	// 测试的是“不要求 AExtractionPlayerState”，不是“完全不需要引擎的玩家身份”。
	Human->SetPlayerState(F.World->SpawnActor<APlayerState>());
	TestEqual(TEXT("Generic map player can be focused without Extraction PlayerState"),
		Engage->ExecuteTask(*F.BT, nullptr), EBTNodeResult::InProgress);
	Engage->WrappedAbortTask(*F.BT, nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardSearchContextTest, "BrokenEdge.Extraction.GuardBT.SearchContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuardSearchContextTest::RunTest(const FString&)
{
	FGuardTaskFixture F;
	if (!TestTrue(TEXT("AI fixture initialized"), F.IsReady())) return false;
	auto* Service = NewObject<UBTService_UpdateGuardContext>(F.Tree);
	Service->InitializeFromAsset(*F.Tree);
	// WrappedTickNode 的调度数据放在 NodeMemory 前方，布局按引擎 GetSpecialNodeMemory 的规则对齐。
	const int32 PrefixSize = Align(static_cast<int32>(Service->GetSpecialMemorySize()), 4);
	TArray<uint8> Memory;
	Memory.SetNumZeroed(PrefixSize + Service->GetInstanceMemorySize() + 1);
	auto TickService = [&]()
	{
		float NextTick = FLT_MAX;
		Service->WrappedTickNode(*F.BT, Memory.GetData() + PrefixSize, .2f, NextTick);
	};
	TickService();
	TestTrue(TEXT("Live guard can act without an Extraction GameMode"), F.BB->GetValueAsBool(GuardBlackboard::CanRunAI));
	TestFalse(TEXT("No remembered target does not start search"), F.BB->GetValueAsBool(GuardBlackboard::SearchActive));
	F.BB->SetValueAsObject(GuardBlackboard::TargetActor, F.Pawn);
	F.BB->SetValueAsBool(GuardBlackboard::HasLastKnownLocation, true);
	F.BB->SetValueAsVector(GuardBlackboard::LastKnownLocation, FVector(700, 100, 0));
	F.BB->SetValueAsFloat(GuardBlackboard::LastSeenTime, static_cast<float>(F.World->GetTimeSeconds()));
	TickService();
	TestNull(TEXT("Lost visible target cleared"), F.BB->GetValueAsObject(GuardBlackboard::TargetActor));
	TestTrue(TEXT("Lost target starts search window"), F.BB->GetValueAsBool(GuardBlackboard::SearchActive));
	TestEqual(TEXT("Search uses unchanged last known location"),
		F.BB->GetValueAsVector(GuardBlackboard::LastKnownLocation), FVector(700, 100, 0));
	F.World->TimeSeconds += 6.;
	TickService();
	TestTrue(TEXT("Pursuit continues beyond previous five-second cutoff"), F.BB->GetValueAsBool(GuardBlackboard::SearchActive));
	F.World->TimeSeconds += GetDefault<UExtractionRules>()->GuardPursuitSeconds;
	TickService();
	TestFalse(TEXT("Search expires at configured pursuit timeout"), F.BB->GetValueAsBool(GuardBlackboard::SearchActive));
	TestTrue(TEXT("Expired search requests return"), F.BB->GetValueAsBool(GuardBlackboard::ReturningToRoute));
	TickService();
	TestFalse(TEXT("Old memory cannot restart search"), F.BB->GetValueAsBool(GuardBlackboard::SearchActive));
	F.BB->SetValueAsObject(GuardBlackboard::TargetActor, F.Pawn);
	F.BB->SetValueAsVector(GuardBlackboard::LastKnownLocation,
		FVector(GetDefault<UExtractionRules>()->GuardPursuitMaxDistance + 1000, 0, 0));
	F.BB->SetValueAsFloat(GuardBlackboard::LastSeenTime, static_cast<float>(F.World->GetTimeSeconds()));
	TickService();
	TestFalse(TEXT("Distant lost target exceeds pursuit budget"), F.BB->GetValueAsBool(GuardBlackboard::SearchActive));
	F.Pawn->bDisableGameplay = true;
	TickService();
	TestFalse(TEXT("Disabled pawn closes action gate"), F.BB->GetValueAsBool(GuardBlackboard::CanRunAI));
	TestFalse(TEXT("Disabled pawn cannot continue returning"), F.BB->GetValueAsBool(GuardBlackboard::ReturningToRoute));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardRandomPatrolBoundsTest, "BrokenEdge.Extraction.GuardBT.RandomPatrolBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuardRandomPatrolBoundsTest::RunTest(const FString&)
{
	FGuardTaskFixture F;
	if (!TestTrue(TEXT("AI fixture initialized"), F.IsReady())) return false;
	F.Guard->bUseRandomPatrol = true;
	F.Guard->Route = nullptr; // 随机模式完全不依赖 Route.Points。
	const FVector Origin = F.Guard->PatrolOrigin;
	F.Guard->RandomPatrolRadius = 1000.f;
	F.Guard->RandomPatrolHeight = 200.f;
	TestTrue(TEXT("Point inside anchored radius allowed"), F.Guard->IsRandomPatrolLocationAllowed(Origin + FVector(700, 0, 0)));
	TestFalse(TEXT("Outside radius rejected"), F.Guard->IsRandomPatrolLocationAllowed(Origin + FVector(1100, 0, 0)));
	TestFalse(TEXT("Capsule clearance reserved at boundary"), F.Guard->IsRandomPatrolLocationAllowed(Origin + FVector(999, 0, 0)));
	TestFalse(TEXT("Another floor rejected"), F.Guard->IsRandomPatrolLocationAllowed(Origin + FVector(0, 0, 400)));
	F.Pawn->SetActorLocation(Origin + FVector(600, 0, 90));
	TestEqual(TEXT("Walking cannot move patrol center"), F.Guard->PatrolOrigin, Origin);
	TestTrue(TEXT("Complete path within range accepted"),
		F.Guard->IsRandomPatrolPathAllowed({Origin, Origin + FVector(400, 0, 0), Origin + FVector(500, 100, 0)}));
	TestFalse(TEXT("In-range goal cannot justify a detour outside range"),
		F.Guard->IsRandomPatrolPathAllowed({Origin, Origin + FVector(1300, 0, 0), Origin + FVector(500, 0, 0)}));
	TestTrue(TEXT("Returning from outside may enter once"),
		F.Guard->IsRandomPatrolPathAllowed({Origin + FVector(1500, 0, 0), Origin + FVector(1200, 0, 0), Origin}));
	TestFalse(TEXT("Returning path cannot leave after entry"),
		F.Guard->IsRandomPatrolPathAllowed({Origin + FVector(1500, 0, 0), Origin, Origin + FVector(1500, 0, 0), Origin}));

	auto* Area = F.World->SpawnActor<AExtractionPatrolArea>();
	Area->Bounds->SetBoxExtent(FVector(600, 300, 150));
	Area->SetActorTransform(FTransform(FRotator(0, 65, 0), FVector(3000, -800, 100), FVector(2, 1, 1)));
	F.Guard->PatrolArea = Area;
	TestTrue(TEXT("Assigned rotated area overrides spawn radius"),
		F.Guard->IsRandomPatrolLocationAllowed(Area->GetActorLocation()));
	TestFalse(TEXT("Old spawn origin is outside assigned area"), F.Guard->IsRandomPatrolLocationAllowed(Origin));
	TestTrue(TEXT("Scaled local interior accepted"), F.Guard->IsRandomPatrolLocationAllowed(
		Area->GetActorTransform().TransformPosition(FVector(500, 200, 0))));
	TestFalse(TEXT("Scaled local exterior rejected"), F.Guard->IsRandomPatrolLocationAllowed(
		Area->GetActorTransform().TransformPosition(FVector(700, 0, 0))));
	for (int32 I = 0; I < 20; ++I)
		TestTrue(TEXT("Area samples are world positions inside rotated box"), Area->ContainsPatrolLocation(Area->SamplePatrolLocation()));

	auto* Set = NewObject<UBTTask_SetPatrolDestination>(F.Tree);
	Set->InitializeFromAsset(*F.Tree);
	F.BB->SetValueAsVector(GuardBlackboard::PatrolLocation, Area->GetActorLocation());
	TestEqual(TEXT("No navigation fails safely instead of inventing a destination"),
		Set->ExecuteTask(*F.BT, nullptr), EBTNodeResult::Failed);
	TestFalse(TEXT("Failed random selection clears stale destination"),
		FAISystem::IsValidLocation(F.BB->GetValueAsVector(GuardBlackboard::PatrolLocation)));

	auto* Complete = NewObject<UBTTask_CompletePatrolArrival>(F.Tree);
	Complete->InitializeFromAsset(*F.Tree);
	F.BB->SetValueAsVector(GuardBlackboard::PatrolLocation, Area->GetActorLocation());
	F.Pawn->SetActorLocation(Area->GetActorLocation() + FVector(0, 0, 90));
	const int32 BeforeIndex = F.Guard->PatrolIndex;
	TestEqual(TEXT("Random arrival completes without any route"), Complete->ExecuteTask(*F.BT, nullptr), EBTNodeResult::Succeeded);
	TestEqual(TEXT("Random arrival does not advance fixed-route index"), F.Guard->PatrolIndex, BeforeIndex);
	TestEqual(TEXT("Duplicate random arrival rejected"), Complete->ExecuteTask(*F.BT, nullptr), EBTNodeResult::Failed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardFacingTest, "BrokenEdge.Extraction.GuardBT.FacingAndPatrolRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuardFacingTest::RunTest(const FString&)
{
	FGuardTaskFixture F;
	if (!TestTrue(TEXT("AI fixture initialized"), F.IsReady())) return false;
	auto* Movement = F.Pawn->GetCharacterMovement();
	// 复现装备/蓝图写入的玩家转向标志，以及原代码把速度写入 Roll 的错误。
	F.Pawn->bUseControllerRotationYaw = true;
	Movement->bOrientRotationToMovement = true;
	Movement->bUseControllerDesiredRotation = false;
	Movement->RotationRate = FRotator(0, 0, 850);
	F.Pawn->RotateInPlace(.1f);
	TestFalse(TEXT("Guard does not copy player camera yaw directly"), F.Pawn->bUseControllerRotationYaw);
	TestFalse(TEXT("Guard movement cannot override Focus rotation"), Movement->bOrientRotationToMovement);
	TestTrue(TEXT("Guard follows controller desired rotation"), Movement->bUseControllerDesiredRotation);
	TestTrue(TEXT("Yaw has nonzero turn speed"), Movement->RotationRate.Yaw > 0);
	TestFalse(TEXT("Player root-bone turn animation disabled for guard"), F.Pawn->ShouldRotateRootBone());

	F.Pawn->SetActorRotation(FRotator::ZeroRotator);
	F.Guard->SetFocalPoint(F.Pawn->GetActorLocation() + FVector(0, 500, 0), EAIFocusPriority::Move);
	for (int32 I = 0; I < 5; ++I)
	{
		F.Guard->UpdateControlRotation(.1f, true);
		Movement->PhysicsRotation(.1f);
	}
	TestTrue(TEXT("Patrol body turns toward movement Focus"),
		FMath::Abs(FMath::FindDeltaAngleDegrees(F.Pawn->GetActorRotation().Yaw, 90.f)) < 1.f);
	auto* Target = F.World->SpawnActor<ABrokenEdgeCharacter>(F.Pawn->GetActorLocation() + FVector(-500, 0, 0), FRotator::ZeroRotator);
	F.Guard->SetFocus(Target);
	TestEqual(TEXT("Guard hand aim uses Gameplay Focus instead of empty player crosshair"), F.Pawn->GetHitTarget(), Target->GetActorLocation());
	for (int32 I = 0; I < 5; ++I)
	{
		F.Guard->UpdateControlRotation(.1f, true);
		Movement->PhysicsRotation(.1f);
	}
	TestTrue(TEXT("Stationary body turns toward player Focus"),
		FMath::Abs(FMath::FindDeltaAngleDegrees(F.Pawn->GetActorRotation().Yaw, 180.f)) < 1.f);
	F.Guard->ClearFocus(EAIFocusPriority::Gameplay);
	for (int32 I = 0; I < 5; ++I)
	{
		F.Guard->UpdateControlRotation(.1f, true);
		Movement->PhysicsRotation(.1f);
	}
	TestTrue(TEXT("After combat body returns to movement Focus"),
		FMath::Abs(FMath::FindDeltaAngleDegrees(F.Pawn->GetActorRotation().Yaw, 90.f)) < 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardStandaloneCombatTest, "BrokenEdge.Extraction.GuardBT.StandaloneFireAndReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuardStandaloneCombatTest::RunTest(const FString&)
{
	FGuardTaskFixture F;
	if (!TestTrue(TEXT("AI fixture initialized"), F.IsReady())) return false;
	// 临时 World 未执行 InitializeActorsForPlay；装备同步需要真实 PostInitializeComponents 中的组件反向引用。
	if (!F.Pawn->IsActorInitialized()) F.Pawn->PostInitializeComponents();
	F.World->GetTimerManager().ClearAllTimersForObject(F.Guard);
	auto* Combat = F.Pawn->GetExtractionCombat();
	TestNull(TEXT("Standalone test has no Extraction GameState"), F.World->GetGameState());
	TestTrue(TEXT("Standalone guard combat is enabled"), Combat->IsEnabled());
	TestTrue(TEXT("Standalone server-controlled guard can act"), Combat->CanAct());
	auto* Human = F.World->SpawnActor<ABrokenEdgeCharacter>(FVector(600, 0, 0), FRotator::ZeroRotator);
	Human->SetPlayerState(F.World->SpawnActor<APlayerState>());
	TestFalse(TEXT("Ordinary map player keeps legacy combat protocol"), Human->GetExtractionCombat()->IsEnabled());
	TestFalse(TEXT("Ordinary player cannot use guard permission"), Human->GetExtractionCombat()->CanAct());
	F.Pawn->bDisableGameplay = true;
	TestFalse(TEXT("Disabled guard cannot act"), Combat->CanAct());
	F.Pawn->bDisableGameplay = false;
	F.Pawn->GuardInventory->InitializeGuard();
	auto* Weapon = F.Pawn->GetEquippedWeapon();
	if (!TestNotNull(TEXT("Real inventory AK equipped"), Weapon)) return false;
	const int32 InitialAmmo = Weapon->GetAmmo();
	TestTrue(TEXT("New guard has no accepted-shot history"), Combat->GetTestShotTimes().IsEmpty());
	F.BB->SetValueAsObject(GuardBlackboard::TargetActor, Human);
	F.BB->SetValueAsFloat(GuardBlackboard::ReactionReadyTime, 0);
	F.World->TimeSeconds = 10;
	auto* Engage = NewObject<UBTTask_EngageTarget>(F.Tree);
	Engage->InitializeFromAsset(*F.Tree);
	Engage->ForceInstancing(false);
	TestEqual(TEXT("Standalone engagement stays active"), Engage->ExecuteTask(*F.BT, nullptr), EBTNodeResult::InProgress);
	const FRotator ControlBefore = F.Guard->GetControlRotation();
	double FirstShotAt = Combat->GetAcceptedShots() ? F.World->GetTimeSeconds() : -1;
	double SecondShotAt = -1;
	// 只推进临时 World 的时钟，避免执行用户地图或其 BeginPlay/行为树资产。
	for (int32 I = 0; I < 200 && Combat->GetAcceptedShots() < 2; ++I)
	{
		F.World->TimeSeconds += .01;
		float NextTick = FLT_MAX;
		const uint32 CountBefore = Combat->GetAcceptedShots();
		Engage->WrappedTickTask(*F.BT, nullptr, .01f, NextTick);
		if (Combat->GetAcceptedShots() > CountBefore)
		{
			if (FirstShotAt < 0) FirstShotAt = F.World->GetTimeSeconds();
			else SecondShotAt = F.World->GetTimeSeconds();
		}
	}
	TestTrue(TEXT("Task fires real accepted shots without Extraction GameState"), Combat->GetAcceptedShots() >= 2);
	const float ConfiguredInterval = FMath::Max(Weapon->FireDelay, GetDefault<UExtractionRules>()->GuardShotInterval);
	AddInfo(FString::Printf(TEXT("Player weapon FireDelay=%.4f s; actual AI spacing=%.4f s"),
		Weapon->FireDelay, SecondShotAt - FirstShotAt));
	TestTrue(TEXT("Actual AI shot spacing respects configured interval"),
		FirstShotAt >= 0 && SecondShotAt - FirstShotAt >= ConfiguredInterval - .001);
	TestTrue(TEXT("AI cadence is not delayed beyond normal polling quantization"),
		FirstShotAt >= 0 && SecondShotAt - FirstShotAt <= ConfiguredInterval + .021);
	const auto& ShotTimes = Combat->GetTestShotTimes();
	TestEqual(TEXT("Each successful shot creates one authority event"),
	          static_cast<uint32>(ShotTimes.Num()), Combat->GetAcceptedShots());
	if (TestTrue(TEXT("Authority history includes both real shots"), ShotTimes.Num() >= 2))
	{
		TestEqual(TEXT("First history sequence matches first accepted shot"), ShotTimes[0].Key, 1u);
		TestEqual(TEXT("Latest history sequence matches accepted count"), ShotTimes.Last().Key,
		          Combat->GetAcceptedShots());
		TestEqual(TEXT("First event preserves actual server fire time"), ShotTimes[0].Value, FirstShotAt);
		TestEqual(TEXT("Second event preserves actual server fire time"), ShotTimes[1].Value, SecondShotAt);
		TestTrue(TEXT("Authority times increase and respect equipped guard interval"),
		         ShotTimes[1].Value > ShotTimes[0].Value
		         && ShotTimes[1].Value - ShotTimes[0].Value >= ConfiguredInterval - .001);
	}
	TestEqual(TEXT("Real shots consume magazine ammunition"), Weapon->GetAmmo(), InitialAmmo - static_cast<int32>(Combat->GetAcceptedShots()));
	TestTrue(TEXT("Recoil does not overwrite AI Focus rotation"), F.Guard->GetControlRotation().Equals(ControlBefore, .001f));
	TestTrue(TEXT("AI next-fire deadline advances after accepted shot"),
		F.BB->GetValueAsFloat(GuardBlackboard::NextFireTime) > F.World->GetTimeSeconds());
	// Immediately retry through the production RPC while the weapon cooldown is active.
	const uint32 AcceptedBeforeRetry = Combat->GetAcceptedShots();
	const uint32 RejectedBeforeRetry = Combat->GetRejectedShots();
	const int32 EventsBeforeRetry = ShotTimes.Num();
	Combat->FireGuard(Human->GetActorLocation());
	TestEqual(TEXT("Immediate duplicate-time fire is rejected by weapon cooldown"),
	          Combat->GetRejectedShots(), RejectedBeforeRetry + 1);
	TestEqual(TEXT("Rejected RPC cannot increase accepted count"), Combat->GetAcceptedShots(), AcceptedBeforeRetry);
	TestEqual(TEXT("Rejected RPC cannot append an authority shot event"), Combat->GetTestShotTimes().Num(),
	          EventsBeforeRetry);
	Engage->WrappedAbortTask(*F.BT, nullptr);
	Combat->EquipmentAmmo(Weapon, 0);
	const int32 ReserveBefore = F.Pawn->GetCombat()->GetCarriedAmmo();
	Combat->BeginServerReload();
	TestEqual(TEXT("Standalone guard starts server reload"), F.Pawn->GetCombatState(), ECombatState::ECS_Reloading);
	{
		// TimerManager 每个引擎帧只更新一次；独立推进两帧，使 Pending 定时器进入 Active 并完成。
		TGuardValue<uint64> RestoreFrame(GFrameCounter, GFrameCounter);
		for (int32 I = 0; I < 2; ++I)
		{
			++GFrameCounter;
			F.World->TimeSeconds += 5;
			F.World->GetTimerManager().Tick(5.f);
		}
	}
	TestTrue(TEXT("Server reload refills magazine"), Weapon->GetAmmo() > 0);
	TestTrue(TEXT("Server reload consumes guard's reserve"), F.Pawn->GetCombat()->GetCarriedAmmo() < ReserveBefore);
	TestEqual(TEXT("Reload leaves combat idle"), F.Pawn->GetCombatState(), ECombatState::ECS_Unoccuiped);
	TestEqual(TEXT("Ammo refill does not create a shot event"), Combat->GetTestShotTimes().Num(), EventsBeforeRetry);

	// 在正式搜打撤模式中继续保留原阶段校验，不能因普通地图支持而提前开火。
	auto* GS = F.World->SpawnActor<AExtractionGameState>();
	F.World->SetGameState(GS);
	GS->Phase = EExtractionPhase::Waiting;
	TestFalse(TEXT("Waiting Extraction raid still blocks guard"), Combat->CanAct());
	const uint32 Before = Combat->GetAcceptedShots();
	Combat->FireGuard(Human->GetActorLocation());
	TestEqual(TEXT("Waiting raid rejects actual shot request"), Combat->GetAcceptedShots(), Before);
	TestEqual(TEXT("Waiting raid rejection leaves authority history unchanged"), Combat->GetTestShotTimes().Num(),
	          EventsBeforeRetry);
	GS->Phase = EExtractionPhase::Active;
	TestTrue(TEXT("Active Extraction raid allows guard"), Combat->CanAct());
	GS->Phase = EExtractionPhase::Ended;
	TestFalse(TEXT("Ended Extraction raid blocks guard"), Combat->CanAct());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardExtraSpreadTest, "BrokenEdge.Extraction.GuardBT.ExtraSpread",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuardExtraSpreadTest::RunTest(const FString&)
{
	const FVector Origin(170, -430, 120);
	const FVector Direction = FVector(1, 2, .15).GetSafeNormal();
	const FVector Target = Origin + Direction * 1000.;
	TestEqual(TEXT("Zero extra spread exactly preserves weapon endpoint"),
		ExtractionGuardAim::ApplySpread(Origin, Target, 0, 18), Target);
	TestEqual(TEXT("Same server shot seed produces same endpoint"),
		ExtractionGuardAim::ApplySpread(Origin, Target, 3, 18), ExtractionGuardAim::ApplySpread(Origin, Target, 3, 18));
	TestEqual(TEXT("Zero-length aim remains finite"), ExtractionGuardAim::ApplySpread(Origin, Origin, 3, 18), Origin);
	int32 WideMisses = 0;
	for (uint32 Seed = 1; Seed <= 128; ++Seed)
	{
		const FVector End = ExtractionGuardAim::ApplySpread(Origin, Target, 3, Seed);
		const FVector Delta = End - Origin;
		TestTrue(TEXT("Extra spread retains weapon trace distance"), FMath::IsNearlyEqual(Delta.Size(), 1000., .01));
		TestTrue(TEXT("Extra spread remains within configured half-angle"),
			FVector::DotProduct(Direction, Delta.GetSafeNormal()) >= FMath::Cos(FMath::DegreesToRadians(3.001f)));
		const double Sideways = (Delta - Direction * FVector::DotProduct(Delta, Direction)).Size();
		if (Sideways > 20.) ++WideMisses;
		// 相同角误差随距离成比例扩大，而不是只在远距离仍偏移固定 20 cm。
		const FVector Far = ExtractionGuardAim::ApplySpread(Origin, Origin + Direction * 2000., 3, Seed);
		TestTrue(TEXT("Same angular spread scales with target distance"),
			(Far - Origin).Equals(Delta * 2., .01));
	}
	TestTrue(TEXT("Most 10m shots have meaningful extra error rather than near-perfect aim"), WideMisses > 64);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardEnemyMeshFacingTest, "BrokenEdge.Extraction.GuardBT.EnemyBlueprintMeshFacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuardEnemyMeshFacingTest::RunTest(const FString&)
{
	FGuardTaskFixture F;
	if (!TestTrue(TEXT("AI fixture initialized"), F.IsReady())) return false;
	UClass* EnemyClass = StaticLoadClass(ABrokenEdgeCharacter::StaticClass(), nullptr,
		TEXT("/Game/Blueprints/Ai/BP_EnemyCharacter.BP_EnemyCharacter_C"));
	if (!TestNotNull(TEXT("Existing enemy Blueprint loads"), EnemyClass)) return false;
	auto* Enemy = F.World->SpawnActor<ABrokenEdgeCharacter>(EnemyClass, FVector(0, 600, 100), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Existing enemy Blueprint spawns"), Enemy)) return false;
	const FVector MeshLocation = Enemy->GetMesh()->GetRelativeLocation();
	const FVector MeshScale = Enemy->GetMesh()->GetRelativeScale3D();
	F.Guard->Possess(Enemy);
	TestNotNull(TEXT("Existing Stinger mesh remains assigned"), Enemy->GetMesh()->GetSkeletalMeshAsset());
	TestTrue(TEXT("Stinger mesh forward corrected relative to actor"),
		FMath::Abs(FMath::FindDeltaAngleDegrees(Enemy->GetMesh()->GetRelativeRotation().Yaw, -90.f)) < .1f);
	TestEqual(TEXT("Mesh location preserved"), Enemy->GetMesh()->GetRelativeLocation(), MeshLocation);
	TestEqual(TEXT("Mesh scale preserved"), Enemy->GetMesh()->GetRelativeScale3D(), MeshScale);
	Enemy->GuardMeshYawOffset = 15.f;
	Enemy->ConfigureGuardRotation();
	TestTrue(TEXT("Other model forward offsets are configurable"),
		FMath::Abs(FMath::FindDeltaAngleDegrees(Enemy->GetMesh()->GetRelativeRotation().Yaw, 15.f)) < .1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardSightContextTest, "BrokenEdge.Extraction.GuardBT.SightToBlackboard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuardSightContextTest::RunTest(const FString&)
{
	FGuardTaskFixture F;
	if (!TestTrue(TEXT("AI fixture initialized"), F.IsReady())) return false;
	auto* Human = F.World->SpawnActor<ABrokenEdgeCharacter>(FVector(600, 0, 0), FRotator::ZeroRotator);
	Human->SetPlayerState(F.World->SpawnActor<APlayerState>());
	auto* System = UAIPerceptionSystem::GetCurrent(F.World);
	auto* Perception = F.Guard->GetPerceptionComponent();
	if (!TestNotNull(TEXT("Perception system exists"), System) || !Perception) return false;
	System->UpdateListener(*Perception);
	System->RegisterSource<UAISense_Sight>(*Human);
	F.Guard->SetControlRotation(FRotator::ZeroRotator);
	for (int32 I = 0; I < 10; ++I)
	{
		F.World->TimeSeconds += .1;
		System->Tick(.1f);
	}
	TArray<AActor*> Candidates;
	Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Candidates);
	TestTrue(TEXT("Real Sight detects front player within range"), Candidates.Contains(Human));
	auto* Service = NewObject<UBTService_UpdateGuardContext>(F.Tree);
	Service->InitializeFromAsset(*F.Tree);
	const int32 PrefixSize = Align(static_cast<int32>(Service->GetSpecialMemorySize()), 4);
	TArray<uint8> Memory;
	Memory.SetNumZeroed(PrefixSize + Service->GetInstanceMemorySize() + 1);
	auto UpdateContext = [&]()
	{
		float NextTick = FLT_MAX;
		Service->WrappedTickNode(*F.BT, Memory.GetData() + PrefixSize, .2f, NextTick);
	};
	UpdateContext();
	TestEqual(TEXT("Visible player becomes blackboard target"), F.BB->GetValueAsObject(GuardBlackboard::TargetActor),
		static_cast<UObject*>(Human));
	const float ReactAt = F.BB->GetValueAsFloat(GuardBlackboard::ReactionReadyTime);
	TestTrue(TEXT("Acquisition schedules reaction delay"), ReactAt >= F.World->GetTimeSeconds());
	Human->GetCharacterMovement()->Velocity = FVector(300, 40, 0);
	UpdateContext();
	TestEqual(TEXT("Visible target velocity is remembered"), F.Guard->LastVisibleTargetVelocity, FVector(300, 40, 0));
	TestEqual(TEXT("Continuous visibility does not restart reaction delay"),
		F.BB->GetValueAsFloat(GuardBlackboard::ReactionReadyTime), ReactAt);
	const FVector LastKnown = F.BB->GetValueAsVector(GuardBlackboard::LastKnownLocation);
	Human->SetActorLocation(FVector(-600, 0, 0));
	Human->GetCharacterMovement()->Velocity = FVector(-900, -200, 0);
	for (int32 I = 0; I < 10; ++I)
	{
		F.World->TimeSeconds += .1;
		System->Tick(.1f);
	}
	UpdateContext();
	TestNull(TEXT("Player leaving view clears target"), F.BB->GetValueAsObject(GuardBlackboard::TargetActor));
	TestTrue(TEXT("Leaving view starts search"), F.BB->GetValueAsBool(GuardBlackboard::SearchActive));
	TestEqual(TEXT("Search does not track unseen player location"), F.BB->GetValueAsVector(GuardBlackboard::LastKnownLocation), LastKnown);
	TestEqual(TEXT("Hidden target velocity cannot rewrite memory"), F.Guard->LastVisibleTargetVelocity, FVector(300, 40, 0));
	Human->SetActorLocation(FVector(600, 0, 0));
	for (int32 I = 0; I < 10; ++I)
	{
		F.World->TimeSeconds += .1;
		System->Tick(.1f);
	}
	UpdateContext();
	TestEqual(TEXT("Reacquisition restores visible target"), F.BB->GetValueAsObject(GuardBlackboard::TargetActor), static_cast<UObject*>(Human));
	TestFalse(TEXT("Reacquisition cancels pursuit branch"), F.BB->GetValueAsBool(GuardBlackboard::SearchActive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardSightRangeTest, "BrokenEdge.Extraction.GuardBT.ExpandedSightRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuardSightRangeTest::RunTest(const FString&)
{
	FGuardTaskFixture F;
	if (!TestTrue(TEXT("AI fixture initialized"), F.IsReady())) return false;
	UClass* ControllerClass = StaticLoadClass(AExtractionGuard::StaticClass(), nullptr,
		TEXT("/Game/Blueprints/Ai/BP_EnemyAIController.BP_EnemyAIController_C"));
	if (!TestNotNull(TEXT("Existing enemy controller Blueprint loads"), ControllerClass)) return false;
	auto* Guard = F.World->SpawnActor<AExtractionGuard>(ControllerClass);
	Guard->Possess(F.Pawn);
	Guard->SetControlRotation(FRotator::ZeroRotator);
	auto* Human = F.World->SpawnActor<ABrokenEdgeCharacter>(FVector(4500, 0, 0), FRotator::ZeroRotator);
	Human->SetPlayerState(F.World->SpawnActor<APlayerState>());
	auto* System = UAIPerceptionSystem::GetCurrent(F.World);
	auto* Perception = Guard->GetPerceptionComponent();
	if (!TestNotNull(TEXT("Perception system exists"), System) || !Perception) return false;
	const auto* Sight = Cast<UAISenseConfig_Sight>(Perception->GetSenseConfig(UAISense::GetSenseID<UAISense_Sight>()));
	if (!TestNotNull(TEXT("Controller has Sight configuration"), Sight)) return false;
	TestEqual(TEXT("Saved Blueprint receives new 60m acquisition range"), Sight->SightRadius, 6000.f);
	TestEqual(TEXT("Saved Blueprint receives 75m loss range"), Sight->LoseSightRadius, 7500.f);
	System->UpdateListener(*Perception);
	System->RegisterSource<UAISense_Sight>(*Human);
	auto SeesAt = [&](float Distance)
	{
		Human->SetActorLocation(FVector(Distance, 0, 0));
		for (int32 I = 0; I < 10; ++I)
		{
			F.World->TimeSeconds += .1;
			System->Tick(.1f);
		}
		TArray<AActor*> Candidates;
		Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Candidates);
		return Candidates.Contains(Human);
	};
	TestTrue(TEXT("Player at 45m can be acquired beyond old 30m limit"), SeesAt(4500));
	TestTrue(TEXT("Known player at 65m remains visible within loss radius"), SeesAt(6500));
	TestFalse(TEXT("Player beyond 75m is lost"), SeesAt(8000));
	TestFalse(TEXT("Lost player at 65m cannot be newly acquired outside 60m"), SeesAt(6500));
	TestTrue(TEXT("Player reentering 55m can be acquired again"), SeesAt(5500));
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardRandomPatrolNavigationTest, "BrokenEdge.Extraction.GuardBT.RandomPatrolNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuardRandomPatrolNavigationTest::RunTest(const FString&)
{
	FGuardTaskFixture F(true);
	if (!TestTrue(TEXT("Navigation fixture initialized"), F.IsReady())) return false;
	// 临时地板和导航体积，不打开/保存用户地图，也不修改行为树资产。
	auto* Floor = F.World->SpawnActor<AActor>();
	auto* Mesh = NewObject<UStaticMeshComponent>(Floor);
	Floor->SetRootComponent(Mesh);
	Mesh->SetMobility(EComponentMobility::Static);
	Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetCanEverAffectNavigation(true);
	Mesh->RegisterComponent();
	Floor->SetActorScale3D(FVector(50, 50, 1));
	Floor->SetActorLocation(FVector(0, 0, -50));
	auto* Volume = F.World->SpawnActor<ANavMeshBoundsVolume>();
	auto* Builder = NewObject<UCubeBuilder>();
	Builder->X = 6000;
	Builder->Y = 6000;
	Builder->Z = 1000;
	UActorFactory::CreateBrushForVolumeActor(Volume, Builder);
	FNavigationSystem::AddNavigationSystemToWorld(*F.World, FNavigationSystemRunMode::EditorMode);
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(F.World);
	if (!TestNotNull(TEXT("Navigation system exists"), Nav)) return false;
	Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock,
		UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
	Nav->OnNavigationBoundsUpdated(Volume);
	Nav->Build();
	FNavLocation Ground;
	if (!TestTrue(TEXT("Real built NavMesh contains floor"), Nav->ProjectPointToNavigation(FVector::ZeroVector, Ground)))
		return false;
	F.Guard->bUseRandomPatrol = true;
	F.Guard->Route = nullptr;
	F.Pawn->SetActorLocation(FVector(0, 0, 90));
	// 在真实 NavMesh 上验证预判、上限与不可达回退。位置来自可见时的记忆，不传入目标对象。
	FVector PursuitGoal;
	F.Guard->LastVisibleTargetVelocity = FVector(300, 0, 900);
	TestTrue(TEXT("Pursuit predicts reachable destination"),
		F.Guard->BuildPursuitDestination(FVector(300, 0, 0), PursuitGoal));
	TestTrue(TEXT("Pursuit leads along last visible horizontal velocity"), PursuitGoal.X > 600 && PursuitGoal.X < 750);
	TestTrue(TEXT("Pursuit goal projected onto ground"), FMath::Abs(PursuitGoal.Z) < 30);
	F.Guard->LastVisibleTargetVelocity = FVector(10000, 0, 0);
	TestTrue(TEXT("Fast target still produces bounded prediction"),
		F.Guard->BuildPursuitDestination(FVector(300, 0, 0), PursuitGoal));
	TestTrue(TEXT("Prediction displacement capped at six meters"), PursuitGoal.X <= 910);
	TestTrue(TEXT("Off-mesh prediction falls back to last seen point"),
		F.Guard->BuildPursuitDestination(FVector(2300, 0, 0), PursuitGoal));
	TestTrue(TEXT("Fallback stays near actual last seen position"), FMath::Abs(PursuitGoal.X - 2300) < 50);
	TestFalse(TEXT("Unreachable remembered location ends pursuit"),
		F.Guard->BuildPursuitDestination(FVector(6000, 0, 0), PursuitGoal));
	auto* Set = NewObject<UBTTask_SetPatrolDestination>(F.Tree);
	auto* Complete = NewObject<UBTTask_CompletePatrolArrival>(F.Tree);
	Set->InitializeFromAsset(*F.Tree);
	Complete->InitializeFromAsset(*F.Tree);
	TArray<FVector> Destinations;
	for (int32 I = 0; I < 6; ++I)
	{
		if (!TestEqual(TEXT("Random task selects reachable point"), Set->ExecuteTask(*F.BT, nullptr), EBTNodeResult::Succeeded))
			return false;
		const FVector Goal = F.BB->GetValueAsVector(GuardBlackboard::PatrolLocation);
		TestTrue(TEXT("Selected goal remains inside fixed activity range"), F.Guard->IsRandomPatrolLocationAllowed(Goal));
		const auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
			F.Guard, F.Pawn->GetNavAgentLocation(), Goal, F.Pawn);
		TestTrue(TEXT("Actual navigation path is complete and bounded"),
			Path && Path->IsValid() && !Path->IsPartial() && F.Guard->IsRandomPatrolPathAllowed(Path->PathPoints));
		Destinations.Add(Goal);
		if (I == 0)
		{
			F.BB->SetValueAsBool(GuardBlackboard::ReturningToRoute, true);
			Set->ExecuteTask(*F.BT, nullptr);
			TestEqual(TEXT("Return reuses unfinished valid random goal"), F.BB->GetValueAsVector(GuardBlackboard::PatrolLocation), Goal);
		}
		F.Pawn->SetActorLocation(Goal + FVector(0, 0, 90));
		TestEqual(TEXT("Random point arrival completes"), Complete->ExecuteTask(*F.BT, nullptr), EBTNodeResult::Succeeded);
	}
	TestTrue(TEXT("New patrol cycles choose different destinations"), !Destinations[0].Equals(Destinations.Last(), 1.f));
	TestEqual(TEXT("Patrol center stays at original possession position"), F.Guard->PatrolOrigin, FVector::ZeroVector);

	auto* Area = F.World->SpawnActor<AExtractionPatrolArea>(FVector(200, 200, 100), FRotator(0, 30, 0));
	Area->Bounds->SetBoxExtent(FVector(700, 600, 200));
	F.Guard->PatrolArea = Area;
	F.Pawn->SetActorLocation(Area->GetActorLocation());
	TestEqual(TEXT("Explicit activity box selects reachable goal"), Set->ExecuteTask(*F.BT, nullptr), EBTNodeResult::Succeeded);
	TestTrue(TEXT("Projected goal remains inside activity box"),
		F.Guard->IsRandomPatrolLocationAllowed(F.BB->GetValueAsVector(GuardBlackboard::PatrolLocation)));
	return true;
}
#endif
#endif
