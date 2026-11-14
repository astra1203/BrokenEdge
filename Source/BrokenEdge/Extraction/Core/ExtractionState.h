#pragma once

#include "CoreMinimal.h"
#include "BrokenEdge/GameState/BrokenEdgeGameState.h"
#include "BrokenEdge/PlayerState/BrokenEdgePlayerState.h"
#include "BrokenEdge/Extraction/Progression/ExtractionProgression.h"
#include "ExtractionState.generated.h"
// 对局状态与玩家状态是服务器到客户端的可复制读模型；实际规则由 GameMode 等服务器逻辑执行。
// GameState 描述整局，PlayerState 描述单人结果和背包，不把对局状态绑定在可销毁的角色 Pawn 上。

// 阶段依次为等待、活动、结束；与玩家的生命/离局状态是两套不同状态。
UENUM(BlueprintType)
enum class EExtractionPhase : uint8 { Waiting, Active, Ended };

// 玩家可处于存活、死亡、离开、撤离、超时或倒地；倒地尚未完成结算，仍可救援。
UENUM(BlueprintType)
enum class EExtractionLife : uint8 { Alive, Dead, Left, Extracted, TimedOut, Downed };

UCLASS()
class BROKENEDGE_API AExtractionGameState : public ABrokenEdgeGameState
{
	GENERATED_BODY()

public:
	UPROPERTY(Replicated, BlueprintReadOnly)
	TObjectPtr<class UExtractionMapDefinition> MapDefinition;
	// 注册本类需要同步的属性并保留父类复制项；客户端读取状态，不在复制回调中做仓库结算。
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(Replicated, BlueprintReadOnly)
	EExtractionPhase Phase = EExtractionPhase::Waiting;
	// 服务器绝对截止时间（秒）；活动阶段表示本局结束，结束阶段表示返回大厅。
	UPROPERTY(Replicated, BlueprintReadOnly)
	double Deadline = 0;
	UPROPERTY(Replicated, BlueprintReadOnly)
	int32 AlivePlayers = 0;
	UPROPERTY(Replicated, BlueprintReadOnly)
	FString EndReason;
	UPROPERTY(Replicated)
	FString StorageError;
	/** Persistent for this match only; late join and region reload apply without replaying FX. */
	UPROPERTY(ReplicatedUsing=OnRep_BrokenGlass)
	TArray<FGuid> BrokenGlass;
	UFUNCTION()
	void OnRep_BrokenGlass();
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastGlassBreak(FGuid PaneId, FVector_NetQuantize Impact, FVector_NetQuantizeNormal Direction);
};

UCLASS()
class BROKENEDGE_API AExtractionPlayerState : public ABrokenEdgePlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(Replicated, BlueprintReadOnly)
	EExtractionLife Life = EExtractionLife::Alive;
	// 撤离完成的服务器绝对时间，0 表示没有进行中的撤离倒计时。
	UPROPERTY(Replicated)
	double ExtractAt = 0;
	// 记录当前计时所属撤离点，换点不能继承另一处的停留进度。
	UPROPERTY(Replicated)
	FName ExtractExitId;
	/** 站在撤离区内但暂时无法撤离的原因，例如丢包撤要求先丢掉背包。 */
	UPROPERTY(Replicated)
	FString ExtractBlockedReason;
	// 倒地流血截止时间（秒），与 DownHealth 的再次受伤耗尽条件并行判断。
	UPROPERTY(Replicated)
	double BleedAt = 0;
	UPROPERTY(Replicated)
	float DownHealth = 0;
	// 局内任务进度仅在成功撤离时由持久化结算写入长期成长。
	UPROPERTY(Replicated)
	FExtractionRaidProgress RaidProgress;
	// 创建玩家背包默认子对象，使物品状态随 PlayerState 管理。
	AExtractionPlayerState();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<class UExtractionInventoryComponent> Inventory;
};
