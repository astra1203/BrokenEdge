#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionMapDefinition.h"
#include "ExtractionExit.generated.h"
// 撤离点 Actor 保存区域、开放时段和已使用名额，并将这些状态复制给客户端。
// 这里只提供可用性与占额操作；玩家停留计时和物资结算由 ExtractionGameMode 负责。

UCLASS()
class BROKENEDGE_API AExtractionExit : public AActor
{
	GENERATED_BODY()
public:
	// 创建无碰撞的区域标记与文字；AlwaysRelevant 让远处客户端也可获得撤离点状态。
	AExtractionExit();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	// 注册定义、开局时间和使用名额的复制；声明 Replicated 还需要在此加入复制列表。
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// 仅服务器接受有效定义，设置 Actor 变换并主动更新本地表现；客户端通过 OnRep 更新。
	void Configure(const FExtractionExitDefinition& InDefinition);
	// 服务器记录开局时间并清零名额；开放与关闭时间均相对该时刻计算。
	void StartRound(double ServerTime);
	// 复用不可用原因作为单一判断依据，避免 UI 提示与服务器条件分叉。
	bool Available(double ServerTime) const;
	// 依次检查定义、名额、开放和关闭时刻；空字符串表示当前可用，关闭边界包含截止时刻。
	FString UnavailableReason(double ServerTime) const;
	// 服务器再次验证当前可用性后增加已用名额；Capacity 为零表示无限制，并非不统计人数。
	bool Consume();
	UPROPERTY(ReplicatedUsing=OnRep_Definition, BlueprintReadOnly) FExtractionExitDefinition Definition;
	UPROPERTY(Replicated, BlueprintReadOnly) double RoundStartedAt = -1;
	UPROPERTY(Replicated, BlueprintReadOnly) int32 UsedCapacity = 0;
	// 电闸全部拉下的服务器时刻；-1 表示尚未通电，仅电闸式撤离点使用。
	UPROPERTY(Replicated, BlueprintReadOnly) double SwitchedOpenAt = -1;
	UPROPERTY(Replicated) TArray<TObjectPtr<class AExtractionExitSwitch>> Switches;
	// 服务器在电闸拉下后调用；本局启用的电闸全部拉下时记录开放时刻。
	void SwitchPulled(double ServerTime);
	int32 PulledSwitches() const;
	// 把世界位置变换到撤离点局部坐标，再减去 BoundsOffset 检测半尺寸；不是依靠碰撞重叠事件。
	bool Contains(const FVector& Position) const;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Marker;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UTextRenderComponent> Label;
private:
	// 根据收到的定义更新地面标记大小和停留秒数文字，不在此推进撤离逻辑。
	UFUNCTION() void OnRep_Definition();
};
