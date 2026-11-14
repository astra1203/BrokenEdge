#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionPatrolArea.generated.h"

class UBoxComponent;

/**
 * 可选的随机巡逻活动区域。放置到关卡后调整 Box Extent，再分配给守卫 Controller.PatrolArea。
 * 这里只定义允许选点/寻路的范围，不产生碰撞，不生成导航，也不会阻挡玩家或子弹。
 * 支持平移、旋转和非均匀缩放；真正可行走部分仍由区域内的 NavMesh 决定。
 */
UCLASS(BlueprintType, Blueprintable)
class BROKENEDGE_API AExtractionPatrolArea : public AActor
{
	GENERATED_BODY()

public:
	AExtractionPatrolArea();

	/** 在详情面板调整 Box Extent 即可改变活动区域，无需逐个放置巡逻点。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AI|Patrol")
	TObjectPtr<UBoxComponent> Bounds;

	/** 检查世界坐标是否在区域中；水平内缩用于预留角色胶囊半径。 */
	UFUNCTION(BlueprintPure, Category="AI|Patrol")
	bool ContainsPatrolLocation(const FVector& Location, float HorizontalInset = 0.f) const;

	/** 在盒子中心平面上随机取样；调用方还必须将它投影到 NavMesh 并检查完整路径。 */
	FVector SamplePatrolLocation() const;
};
