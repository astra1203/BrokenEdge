#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "TimerManager.h"
#include "ExtractionGuard.generated.h"

class ABrokenEdgeCharacter;
class UAISenseConfig_Sight;
class UBehaviorTree;
class AExtractionPatrolArea;

UCLASS()
class BROKENEDGE_API AExtractionPatrolRoute : public AActor
{
	GENERATED_BODY()

public:
	AExtractionPatrolRoute();
	/** World-space points, authored and projected to the cooked navigation mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FVector> Points;
};

UENUM()
enum class EExtractionGuardState : uint8 { Patrol, Engage, Search, Return, Dead };

UCLASS()
class BROKENEDGE_API AExtractionGuard : public AAIController
{
	GENERATED_BODY()

public:
	// 不再重写 Tick：AI Perception 提供视觉候选，BT Service 筛选目标，Task 执行动作。
	// 仍保留 AAIController 自身的引擎更新，供朝向等基础控制逻辑使用。
	AExtractionGuard();
	/**
	 * 接管角色后自动调用：初始化守卫装备，再运行 GuardBehaviorTree 及其关联黑板。
	 * 角色尚未 BeginPlay 时延后一帧；重复调用不会重复发放装备或重启正在运行的树。
	 * 返回 false 时查看 EXTRACTION_GUARD_BT 日志，定位角色或资产配置问题。
	 */
	UFUNCTION(BlueprintCallable, Category="AI|Behavior Tree")
	bool StartGuardAI();
	/** 死亡或解除控制时停止行为树、移动和关注，清除尚未执行的启动回调。 */
	UFUNCTION(BlueprintCallable, Category="AI|Behavior Tree")
	void StopGuardAI();

	/** 默认在活动范围内随机巡逻；关闭后沿用 Route.Points 的固定顺序巡逻。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Patrol")
	bool bUseRandomPatrol = true;

	/** 可选活动盒。由角色蓝图在 Event Possessed 中 Set Patrol Area；为空则使用初始位置和半径。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Patrol")
	TObjectPtr<AExtractionPatrolArea> PatrolArea;

	/** 未指定活动盒时，以初次接管此角色的位置为圆心。单位 cm，不会随着每次选点向外漂移。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Patrol", meta=(ClampMin="100.0", Units="cm"))
	float RandomPatrolRadius = 1000.f;

	/** 圆形范围允许的地面高度差，避免随机投影到相邻楼层。指定活动盒时改用盒子的高度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Patrol", meta=(ClampMin="1.0", Units="cm"))
	float RandomPatrolHeight = 200.f;

	/** 服务器接管角色时记录的固定中心；运行时可查看，不能通过蓝图随意改写。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="AI|Patrol")
	FVector PatrolOrigin = FAISystem::InvalidLocation;

	/** 随机巡逻的目标点和路径都要通过同一范围检查。预留角色胶囊半径。 */
	bool IsRandomPatrolLocationAllowed(const FVector& Location) const;
	/** 允许从区域外返回，但路径一旦进入活动范围，就不能再次离开。 */
	bool IsRandomPatrolPathAllowed(const TArray<FVector>& PathPoints) const;
	/** 只由视觉 Service 在实际看见目标时更新，每名守卫独立保存，不读取隐藏目标速度。 */
	FVector LastVisibleTargetVelocity = FVector::ZeroVector;
	/** 丢失视野时计算一次追击点：速度预判、导航投影、完整路径及距离预算校验。 */
	bool BuildPursuitDestination(const FVector& LastSeenLocation, FVector& OutDestination) const;
	/**
	 * 巡逻任务读取的路线，可由生成逻辑或角色蓝图分配。
	 * 手动放置的敌人可在 Event Possessed 中取得此控制器，再使用 Set Route 传入关卡路线实例。
	 * 仅在关闭 Use Random Patrol 时读取路线点；随机模式不要求分配 Route。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Patrol")
	TObjectPtr<AExtractionPatrolRoute> Route;
	// 当前可见目标的诊断镜像，由感知 Service 写入；行为树应读取黑板中的目标键。
	UPROPERTY()
	TObjectPtr<ABrokenEdgeCharacter> Target;
	// 保留原测试接口；状态、射击和到点计数不再由 Controller Tick 自动更新。
	EExtractionGuardState State = EExtractionGuardState::Patrol;
	int32 PatrolIndex = 0;
	int32 VisibleChecks = 0, Shots = 0, PatrolArrivals = 0;
	/**
	 * 交战走位点：bCover 为真时找威胁者看不到的可达掩体，否则找能看到威胁者的横移点。
	 * 都要求完整导航路径；找不到返回 false，交战任务保持站定射击。
	 */
	bool FindTacticalPoint(const APawn* Threat, bool bCover, FVector& Out) const;
	/** 服务器：通知 Radius 内的守卫听到 Location 处的枪声。 */
	static void HearShot(UWorld* World, const FVector& Location, float Radius);
	/** 当前没有可见目标时，把枪声位置作为搜索点进入搜索；已在交战的守卫不受影响。 */
	void Hear(const FVector& Location);
	// 交战走位的每名守卫状态：下次换位时刻、是否正在找掩体及掩体冷却截止时刻。
	double NextTacticAt = 0, CoverUntil = 0, NextCoverAt = 0;
	bool bTakingCover = false;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * 默认使用 /Game/Blueprints/Ai/BT_ExtractionGuard。
	 * 如以后使用控制器蓝图子类，可以在类默认值中选择其他行为树。
	 * 当前 SpawnGuards 创建原生控制器，因此使用这里的 C++ 默认资产引用。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Behavior Tree")
	TObjectPtr<UBehaviorTree> GuardBehaviorTree;

	/** 视觉参数子对象；距离由 ExtractionRules 配置应用，半视角与阵营可在控制器蓝图中调整。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AI|Perception")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

private:
	/** 构造和接管角色时应用配置距离，防止旧控制器蓝图子对象仍保留 3000 cm。 */
	void ApplySightRules();
	/** OnPossess 可能早于角色组件初始化完成，因此用下一帧回调启动，而不是 Tick 轮询。 */
	void InitializeGuardAI();
	FTimerHandle StartGuardAITimer;
	TWeakObjectPtr<APawn> PatrolOriginPawn;
};
