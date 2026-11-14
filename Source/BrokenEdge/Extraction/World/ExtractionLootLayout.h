#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ExtractionLootLayout.generated.h"
// 布局记录点位来源，Recipe 记录项目掉落内容，两者分离以免把兼容调参误称为源游戏规则。
// 服务器开局按固定种子选择点位；组名为空的固定点全选，有组名的候选按权重每组选一个。

USTRUCT(BlueprintType)
struct BROKENEDGE_API FExtractionLootStack
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Item;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) int32 Quantity = 1;
};

/** Project drop tuning stays separate from source point provenance. */
UCLASS(BlueprintType)
class BROKENEDGE_API UExtractionLootRecipe : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FExtractionLootStack> Items;
	/**
	 * 随机收集品：每局在固定 Items 之外再从收集品池抽 [X, Y] 件，按品质（DisplayRarity 0-5）权重挑选，
	 * PoolCategories 非空时只抽这些 DisplayCategory。Y 为 0 表示不抽。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pool") FIntPoint PoolRolls = FIntPoint::ZeroValue;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pool") TArray<FName> PoolCategories;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pool") TArray<float> QualityWeights = {40, 30, 16, 9, 4, 1};
	// 按种子抽取收集品定义 ID；池为空或未配置时返回空数组。
	TArray<FName> RollPool(FRandomStream& Random) const;
};

USTRUCT(BlueprintType)
struct BROKENEDGE_API FExtractionLootPoint
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Id;
	/** Empty means a fixed point. Each named group selects exactly one candidate per raid. */
	// 空组为必选固定点，非空组内按 Weight 竞争一个名额；打开容器不会重抽。
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Group;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Weight = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool Enabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform Transform;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText Label;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Kind;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Reference;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<class USkeletalMesh> Appearance;
	/** 静态网格外观（如航空箱），设置后优先于 Appearance；LidAppearance 为可掀开的盖子，LidHinge 是铰链在箱体网格中的位置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<class UStaticMesh> StaticAppearance;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<class UStaticMesh> LidAppearance;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector LidHinge = FVector::ZeroVector;
	// 内容配方引用与外观、位置来源独立；配方缺失时该容器可能为空。
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UExtractionLootRecipe> Recipe;
};

/** One source variant only. Selection is authority-owned and never rerolled by opening a container. */
UCLASS(BlueprintType)
class BROKENEDGE_API UExtractionLootLayout : public UDataAsset
{
	GENERATED_BODY()

public:
	/** A point whose Kind equals this is a loose-item site: spawn picks one Recipe stack at random. */
	static const FName LooseKind;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Variant;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString GroupPolicy;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FExtractionLootPoint> Points;
	/** Loose sites filled per raid, drawn at random from every enabled LooseKind point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	int32 LooseSpawnCount = 10;
	// 校验唯一 ID、变换和权重后，对组名及点位排序再按种子抽取；排序避免容器遍历顺序影响重现。
	// 输出是 Points 索引，失败时清空选择并返回错误文本。
	bool Select(int32 Seed, TArray<int32>& Out, FString& Error) const;
	// 只在服务器生成选中的容器，沿用配置高程并关闭默认落地/种子逻辑；配方异常记录警告。
	void Spawn(class UWorld* World, int32 Seed) const;

private:
	/** Outdoor scatter is sampled separately so it never competes with authored containers. */
	void SpawnLoose(class UWorld* World, int32 Seed) const;
};
