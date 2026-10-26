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
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Variant;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString GroupPolicy;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FExtractionLootPoint> Points;
	// 校验唯一 ID、变换和权重后，对组名及点位排序再按种子抽取；排序避免容器遍历顺序影响重现。
	// 输出是 Points 索引，失败时清空选择并返回错误文本。
	bool Select(int32 Seed, TArray<int32>& Out, FString& Error) const;
	// 只在服务器生成选中的容器，沿用配置高程并关闭默认落地/种子逻辑；配方异常记录警告。
	void Spawn(class UWorld* World, int32 Seed) const;
};
