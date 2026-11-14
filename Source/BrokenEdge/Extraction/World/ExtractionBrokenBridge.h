#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionBrokenBridge.generated.h"
// 发射区断桥（项目自定规则）：关卡里若干段桥面作为候选，Variants 定义几种断法（每种是一组要断开的段），
// 服务器每局随机选一种并复制到各端。断开的段原模型收起，原位置换成一块向下倾斜的坍塌桥板（同一模型、带碰撞），
// 所有玩家看到并走到的是同一座断桥。候选段位置见 place_map_events.py。

USTRUCT()
struct FExtractionBridgePart
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	TObjectPtr<AActor> Actor;
	UPROPERTY(EditAnywhere)
	FName Component;
	/** 实例化网格的实例编号；-1 表示整个组件。 */
	UPROPERTY(EditAnywhere)
	int32 Instance = -1;
};

USTRUCT()
struct FExtractionBridgeSpan
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	FText Label;
	UPROPERTY(EditAnywhere)
	TArray<FExtractionBridgePart> Parts;
};

UCLASS()
class BROKENEDGE_API AExtractionBrokenBridge : public AActor
{
	GENERATED_BODY()

public:
	AExtractionBrokenBridge();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(EditAnywhere, Category="BrokenBridge")
	TArray<FExtractionBridgeSpan> Spans;
	/** 断法：每项是要断开的段编号位掩码（第 i 位对应 Spans[i]）。默认两种：只断中段；北段和南段同时断。 */
	UPROPERTY(EditAnywhere, Category="BrokenBridge")
	TArray<int32> Variants = {0b010, 0b101};
	/** 坍塌桥板绕一端下垂的角度。 */
	UPROPERTY(EditAnywhere, Category="BrokenBridge")
	float CollapsePitch = 18.f;
	/** 本局选中的断法编号；-1 表示尚未选择。命令行 ExtractionBrokenBridge=N 可固定，便于测试。 */
	UPROPERTY(ReplicatedUsing=OnRep_BrokenSpan, VisibleInstanceOnly, Category="BrokenBridge")
	int32 BrokenSpan = -1;

private:
	bool bApplied = false;
	// 在一段桥面原位置生成向下倾斜的坍塌桥板。
	void SpawnCollapsed(UStaticMesh* Mesh, const FTransform& Original, int32 Index);
	UFUNCTION()
	void OnRep_BrokenSpan();
	void Apply();
};
