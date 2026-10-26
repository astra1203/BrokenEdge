#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ExtractionMapDefinition.generated.h"
// 地图 DataAsset 是编辑器配置，不是玩家运行时存档：集中描述出生组、撤离点、概览范围与战利品布局。
// EditAnywhere 允许编辑配置，BlueprintReadOnly 允许蓝图读取；这些标记本身不意味着网络复制。

/** Evidence describes the authored location/rule, not whether runtime tests passed. */
// Temporary/ Inferred/ Verified 表示配置来源证据等级，不表示自动化测试通过情况。
UENUM(BlueprintType)
enum class EExtractionReferenceStatus : uint8 { Temporary, Inferred, Verified };

// 小队出生组：Positions 提供组内不同成员的位置，GameMode 再做地面与胶囊检查。
USTRUCT(BlueprintType)
struct BROKENEDGE_API FExtractionSpawnGroup
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FTransform> Positions;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EExtractionReferenceStatus ReferenceStatus = EExtractionReferenceStatus::Temporary;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Reference;
};

// 撤离规则数据：Transform 是世界变换，HalfExtent 和 BoundsOffset 在该局部坐标系内，长度单位为厘米。
USTRUCT(BlueprintType)
struct BROKENEDGE_API FExtractionExitDefinition
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText Label;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform Transform;
	/** Bounds are relative to Transform; the origin is at ground level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector HalfExtent = FVector(250, 250, 132.5);
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector BoundsOffset = FVector(0, 0, 117.5);
	// 玩家需要连续停留的秒数；离开区域或切换撤离点会重新计时。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.1"))
	float HoldSeconds = 10;
	// 相对开局的开放延迟（秒）；ClosesAfterSeconds 为 0 时不设关闭时刻。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	float OpensAfterSeconds = 0;
	/** Zero means no close deadline / no capacity limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	float ClosesAfterSeconds = 0;
	// 可成功撤离的总名额，0 表示不限制；使用计数保存在撤离点 Actor 中。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	int32 Capacity = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EExtractionReferenceStatus ReferenceStatus = EExtractionReferenceStatus::Temporary;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Reference;

	// 校验撤离点标识、有限数值、正尺寸与合法时间窗口；配置合格不代表出生或通行已做运行时验证。
	bool IsValid() const
	{
		return !Id.IsNone() && !Transform.ContainsNaN() && !HalfExtent.ContainsNaN() && !BoundsOffset.ContainsNaN()
			&& HalfExtent.GetMin() > 0 && FMath::IsFinite(HoldSeconds) && HoldSeconds >= .1f
			&& FMath::IsFinite(OpensAfterSeconds) && OpensAfterSeconds >= 0
			&& FMath::IsFinite(ClosesAfterSeconds) && (ClosesAfterSeconds == 0 || ClosesAfterSeconds >
				OpensAfterSeconds)
			&& Capacity >= 0;
	}
};

UCLASS(BlueprintType)
class BROKENEDGE_API UExtractionMapDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	/** World-space floor rectangles from restored geometry, two corners per rectangle. */
	// 每两个二维世界坐标组成一个地板矩形，供战术地图绘制，不能替代场景碰撞。
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FVector2D> FloorBounds;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector2D OverviewMin = FVector2D(-12000,-12000);
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector2D OverviewMax = FVector2D(12000,12000);
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName MapId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString ReferenceBuild;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform SourceToWorld;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FExtractionSpawnGroup> SpawnGroups;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FExtractionExitDefinition> Exits;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<class UExtractionLootLayout> LootLayout;
};
