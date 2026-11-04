#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PhysicsVolume.h"
#include "ExtractionWater.generated.h"
// 可游泳的水体（项目自定）：盒子范围内、当前水面以下视为水体积，角色进入后由引擎切到游泳并受浮力。
// 浮力室默认空池，由电闸开始注水，水面在 FillSeconds 内从池底升到满水；中控桥下的水默认满。
// 注水状态由服务器复制开始时刻，各端按服务器时间插值水面，游泳判定与画面一致。

class UBoxComponent;
class UStaticMeshComponent;

UCLASS()
class BROKENEDGE_API AExtractionWater : public APhysicsVolume
{
	GENERATED_BODY()

public:
	AExtractionWater(const FObjectInitializer& ObjectInitializer);
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// 只有组件中心在当前水面以下才算进入水体；空池时永远不算。
	virtual bool IsOverlapInVolume(const USceneComponent& TestComponent) const override;

	/** 水体范围：根盒子，水平范围与池底到满水水面的整段深度；只与 Pawn 重叠。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> Extent;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Surface;
	/** 水平半尺寸（厘米）。 */
	UPROPERTY(EditAnywhere, Category="Water")
	FVector2D HalfSize = FVector2D(500, 500);
	/** 相对 Actor 原点的池底与满水水面高度（厘米）。 */
	UPROPERTY(EditAnywhere, Category="Water")
	float BottomZ = -300;
	UPROPERTY(EditAnywhere, Category="Water")
	float FullZ = 0;
	/** true：开局即满水；false：空池，等电闸注水。 */
	UPROPERTY(EditAnywhere, Category="Water")
	bool bStartsFull = true;
	/** 不满水开局时的初始水面（相对原点，默认等于池底即空池）；注水从这里涨到 FullZ。 */
	UPROPERTY(EditAnywhere, Category="Water")
	float StartZ = -100000.f;
	UPROPERTY(EditAnywhere, Category="Water", meta=(ClampMin="0.1"))
	float FillSeconds = 25.f;
	/** 注水电闸相对 Actor 原点的位置；为空表示不生成电闸。 */
	UPROPERTY(EditAnywhere, Category="Water")
	TArray<FVector> SwitchLocations;
	UPROPERTY(EditAnywhere, Category="Water")
	FText Label;
	UPROPERTY(EditAnywhere, Category="Water")
	TObjectPtr<class UMaterialInterface> SurfaceMaterial;

	/** 服务器开始注水的时刻；-1 表示还没注水。 */
	UPROPERTY(ReplicatedUsing=OnRep_FillStartedAt)
	double FillStartedAt = -1;
	// 服务器：电闸拉下后调用；已满或正在注水时忽略，返回是否开始。
	bool StartFill();
	bool IsFilling() const { return FillStartedAt >= 0; }
	// 当前水面相对 Actor 原点的高度；空池返回 BottomZ。
	float SurfaceZ() const;
	// 当前水面的世界高度，供浸入深度计算。
	float SurfaceWorldZ() const { return GetActorLocation().Z + SurfaceZ(); }

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<class AExtractionExitSwitch>> Switches;
	UFUNCTION()
	void OnRep_FillStartedAt();
	void ApplyLayout();
	void ApplySurface();
	double ServerNow() const;
};
