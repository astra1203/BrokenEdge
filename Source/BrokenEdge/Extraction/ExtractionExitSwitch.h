#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionExitSwitch.generated.h"
// 电闸式撤离点的开关：撤离点每局随机启用其中几个，玩家按 F 拉下；全部拉下后由撤离点开放计时。
// 开关只保存自身状态并转告所属撤离点，开放与关闭规则仍由 AExtractionExit 统一判断。

UCLASS()
class BROKENEDGE_API AExtractionExitSwitch : public AActor
{
	GENERATED_BODY()

public:
	// 创建无碰撞的闸箱与指示灯；AlwaysRelevant 让战术地图远处也能看到启用状态。
	AExtractionExitSwitch();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UStaticMeshComponent> Lamp;
	// 本局是否被选中；未启用的开关保留外观但不能交互。
	UPROPERTY(ReplicatedUsing=OnRep_State)
	bool bActive = false;
	UPROPERTY(ReplicatedUsing=OnRep_State)
	bool bPulled = false;
	UPROPERTY(Replicated)
	TObjectPtr<class AExtractionExit> Exit;
	// 吊桥的锁：与 Exit 二选一，拉下后放下这座桥。
	UPROPERTY(Replicated)
	TObjectPtr<class AExtractionDrawbridge> Bridge;
	// 注水开关：与 Exit / Bridge 三选一，拉下后这片水体开始注水。
	UPROPERTY(Replicated)
	TObjectPtr<class AExtractionWater> Water;
	// 服务器开局重置：设置是否启用并清除已拉状态。
	void ResetForRound(bool bInActive);
	// 启用、未拉下、两米内且视线无遮挡才可交互。
	bool CanReach(class ABrokenEdgeCharacter* Character) const;
	// 服务器再次检查行动能力与距离后拉下开关并通知撤离点；返回反馈文本。
	FString Pull(class ABrokenEdgeCharacter* Character);
	FString PromptText() const;

private:
	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> LampMaterial;
	// 指示灯：未启用暗灰、待拉红色、已拉绿色。
	UFUNCTION()
	void OnRep_State();
};
