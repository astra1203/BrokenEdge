// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LagCompensationComponent.generated.h"

class ABrokenEdgeCharacter;
class USkeletalMeshComponent;

/**
 * 命中体的一个几何元素，来自角色网格物理资产（Physics Asset）中某个 Body。
 * 形状尺寸在骨骼空间里固定不变，倒带只需要记录骨骼的世界变换。
 */
struct FHitShape
{
	enum class EKind : uint8 { Sphere, Capsule, Box };

	EKind Kind = EKind::Sphere;
	/** 在 Bodies 数组中的下标，即所属骨骼。 */
	int32 Body = INDEX_NONE;
	/** 形状相对骨骼的变换（胶囊轴为局部 Z）。 */
	FTransform Local;
	/** 球/胶囊半径。 */
	float Radius = 0;
	/** 胶囊圆柱段半长。 */
	float HalfLength = 0;
	/** 盒子半尺寸。 */
	FVector HalfExtent = FVector::ZeroVector;
};

/** 某一时刻全部命中体所属骨骼的世界变换，与 ULagCompensationComponent::Bones 一一对应。 */
struct FHitFrame
{
	double Time = 0;
	TArray<FTransform> Bones;
	/** 本帧所有命中体的包围球，用于射线粗筛。 */
	FSphere Bounds = FSphere(ForceInit);
};

/** 一次倒带命中。 */
struct FRewindHit
{
	ABrokenEdgeCharacter* Character = nullptr;
	FName Bone;
	/** 沿查询线段的比例 [0,1]。 */
	float Fraction = 1;
	FVector Location = FVector::ZeroVector;
};

/** 纯几何检测，独立出来便于自动化测试。线段参数化为 Start + t*(End-Start)，t∈[0,1]。 */
namespace HitRewind
{
	BROKENEDGE_API bool SegmentSphere(const FVector& Start, const FVector& End, const FVector& Center, float Radius,
	                                  float& OutT);
	/** 胶囊轴为局部 Z，圆柱段为 [-HalfLength, HalfLength]。 */
	BROKENEDGE_API bool SegmentCapsule(const FVector& Start, const FVector& End, float Radius, float HalfLength,
	                                   float& OutT);
	BROKENEDGE_API bool SegmentBox(const FVector& Start, const FVector& End, const FVector& HalfExtent, float& OutT);
	BROKENEDGE_API bool SegmentShape(const FHitShape& Shape, const FTransform& Bone, const FVector& Start,
	                                 const FVector& End, float Inflate, float& OutT);
	/** 服务器允许的最大倒带时长（秒），由 BrokenEdge.Net.MaxRewindMs 控制。 */
	BROKENEDGE_API double MaxRewindSeconds();
}

/** 霰弹枪服务器端倒带结果结构 */
USTRUCT(BlueprintType)
struct FShotgunServerSideRewindResult
{
	GENERATED_BODY()

	/** 爆头结果映射 */
	UPROPERTY()
	TMap<ABrokenEdgeCharacter*, uint32> HeadShots;

	/** 身体命中结果映射 */
	UPROPERTY()
	TMap<ABrokenEdgeCharacter*, uint32> BodyShots;
};

/** 服务器端倒带结果结构 */
USTRUCT(BlueprintType)
struct FServerSideRewindResult
{
	GENERATED_BODY()

	/** 是否确认命中 */
	UPROPERTY()
	bool bHitConfirmed = false;

	/** 是否为爆头 */
	UPROPERTY()
	bool bHeadShot = false;
};

/**
 * 服务器延迟补偿。
 * 命中形状直接取角色网格的物理资产，与普通子弹打到网格时的判定形状一致；
 * 服务器每帧只记录这些 Body 所属骨骼的世界变换，倒带时对历史姿态做纯几何检测，不移动任何组件。
 * 倒带时长统一限制在 HitRewind::MaxRewindSeconds() 以内。
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BROKENEDGE_API ULagCompensationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULagCompensationComponent();

	friend class ABrokenEdgeCharacter;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	/** 立即记录一帧（服务器）。Tick 会自动调用，测试也可以直接调用。 */
	void SaveFrame();

	/** 用指定时刻（服务器时间）的姿态检测线段，Inflate 为子弹半径。只检测本角色。 */
	bool TraceAt(double Time, const FVector& Start, const FVector& End, float Inflate, FRewindHit& OutHit) const;

	/**
	 * 在世界内所有记录了历史的角色上做倒带线段检测，返回最近的命中。
	 * Ignore 中的角色不检测；当前网格不能被 ECC_Visibility 命中的角色（死亡、关闭碰撞）也跳过。
	 */
	static bool TraceWorldAt(const UWorld* World, double Time, const FVector& Start, const FVector& End,
	                         float Inflate, const TArray<const AActor*>& Ignore, FRewindHit& OutHit);

	/** 世界内参与倒带的角色，供子弹在主扫掠里忽略它们的当前姿态。 */
	static void GetRewindActors(const UWorld* World, TArray<AActor*>& OutActors);

	/** 当前是否能参与倒带检测。 */
	bool CanBeRewound() const;

	int32 GetNumFrames() const { return History.Num(); }
	int32 GetNumShapes() const { return Shapes.Num(); }

	/**
	 * 服务器分数请求（旧版射弹武器，非搜打撤模式）
	 */
	UFUNCTION(Server, Reliable)
	void ProjectileServerScoreRequest(ABrokenEdgeCharacter* HitCharacter, const FVector_NetQuantize& TraceStart,
	                                  const FVector_NetQuantize100& InitialVelocity, float HitTime);

	/**
	 * 服务器分数请求（旧版扫描武器，非搜打撤模式）
	 */
	UFUNCTION(Server, Reliable)
	void HitScanServerScoreRequest(ABrokenEdgeCharacter* HitCharacter, const FVector_NetQuantize& TraceStart,
	                               const FVector_NetQuantize& HitLocation, float HitTime);

	/**
	 * 服务器分数请求（旧版霰弹枪，非搜打撤模式）
	 */
	UFUNCTION(Server, Reliable)
	void ShotgunServerScoreRequest(const TArray<ABrokenEdgeCharacter*>& HitCharacters,
	                               const FVector& TraceStart, const TArray<FVector_NetQuantize>& HitLocations,
	                               float HitTime);

	FServerSideRewindResult HitScanServerSideRewind(ABrokenEdgeCharacter* HitCharacter,
	                                                const FVector_NetQuantize& TraceStart,
	                                                const FVector_NetQuantize& HitLocation, float HitTime);
	FServerSideRewindResult ProjectileServerSideRewind(ABrokenEdgeCharacter* HitCharacter,
	                                                   const FVector_NetQuantize& TraceStart,
	                                                   const FVector_NetQuantize100& InitialVelocity, float HitTime);
	FShotgunServerSideRewindResult ShotgunServerSideRewind(const TArray<ABrokenEdgeCharacter*>& HitCharacters,
	                                                       const FVector& TraceStart,
	                                                       const TArray<FVector_NetQuantize>& HitLocations,
	                                                       float HitTime);

private:
	/** 从网格物理资产建立 Bones/Shapes；物理资产或网格变化时重建。 */
	bool EnsureShapes();
	/** 取 Time 时刻的骨骼姿态（相邻两帧插值），Time 先被夹到倒带上限内。 */
	bool PoseAt(double Time, TArray<FTransform>& OutBones, FSphere& OutBounds) const;
	void ApplyLegacyDamage(ABrokenEdgeCharacter* HitCharacter, float Damage);

	/** 关联的角色 */
	UPROPERTY()
	class ABrokenEdgeCharacter* Character = nullptr;

	/** 关联的玩家控制器 */
	UPROPERTY()
	class ABrokenEdgePlayerController* Controller = nullptr;

	/** 历史保留时长，需大于最大倒带时长。 */
	UPROPERTY(EditAnywhere)
	float MaxRecordTime = .5f;

	TWeakObjectPtr<class UPhysicsAsset> ShapeSource;
	TWeakObjectPtr<class USkinnedAsset> ShapeMesh;
	TArray<FName> Bones;
	TArray<int32> BoneIndices;
	TArray<FHitShape> Shapes;
	/** 任一形状离所属骨骼原点的最远距离，用于包围球。 */
	float ShapeReach = 0;
	/** 按时间升序，末尾最新。 */
	TArray<FHitFrame> History;
};
