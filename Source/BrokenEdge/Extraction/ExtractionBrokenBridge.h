#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionBrokenBridge.generated.h"
// 发射区断桥：关卡里若干段桥面作为候选，服务器每局随机断开其中一段；选中编号复制到各端，
// 各端把这段桥面的模型实例收起并去掉碰撞，所有玩家看到并走到的是同一座断桥。
// 官方解包里没找到断桥的配置，候选段与位置是项目按发射区高架步道选的，见 place_map_events.py。

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
	/** 本局断开的候选编号；-1 表示尚未选择。命令行 ExtractionBrokenBridge=N 可固定，便于测试。 */
	UPROPERTY(ReplicatedUsing=OnRep_BrokenSpan, VisibleInstanceOnly, Category="BrokenBridge")
	int32 BrokenSpan = -1;

private:
	bool bApplied = false;
	UFUNCTION()
	void OnRep_BrokenSpan();
	void Apply();
};
