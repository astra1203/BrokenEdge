#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionDrawbridge.generated.h"
// 东区吊桥（官方 BP_Interactor_BridgeDoor）：两片桥面默认竖起，两端各一把锁；任一端解锁后
// 桥面在 LowerSeconds 内放平，两端挡墙同时撤掉。状态由服务器复制，各端按开始时刻插值转动。
// 组件布局取官方类默认值与关卡实例覆盖值（单位厘米，相对 Others 节点），见 place_map_events.py。

class UStaticMeshComponent;
class UBoxComponent;

UCLASS()
class BROKENEDGE_API AExtractionDrawbridge : public AActor
{
	GENERATED_BODY()

public:
	AExtractionDrawbridge();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Others;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Hinge1;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Hinge2;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Deck1;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Deck2;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> Block1;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> Block2;
	/** 两把锁相对 Others 的位置；BeginPlay 时由服务器生成可交互的电闸。 */
	UPROPERTY(EditAnywhere, Category="Drawbridge")
	TArray<FVector> LockLocations;
	/** 竖起时的桥面翻转角（官方 DoorProxy Roll ±70°），放下为 0。 */
	UPROPERTY(EditAnywhere, Category="Drawbridge")
	float RaisedRoll = 70.f;
	UPROPERTY(EditAnywhere, Category="Drawbridge", meta=(ClampMin="0.1"))
	float LowerSeconds = 6.f;
	UPROPERTY(EditAnywhere, Category="Drawbridge")
	FText Label;
	/** 服务器开始放桥的时刻；-1 表示仍竖起。 */
	UPROPERTY(ReplicatedUsing=OnRep_LoweredAt)
	double LoweredAt = -1;
	// 服务器：由锁调用，开始放下桥面；已放下或正在放下时忽略。
	void Lower();
	bool IsLowering() const { return LoweredAt >= 0; }

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<class AExtractionExitSwitch>> Locks;
	UFUNCTION()
	void OnRep_LoweredAt();
	// 按当前时间设置两片桥面角度；放平后撤掉挡墙。
	void ApplyPose();
};
