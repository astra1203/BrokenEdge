#pragma once

#include "CoreMinimal.h"
#include "BrokenEdge/GameMode/BrokenEdgeGameMode.h"
#include "ExtractionPersistence.h"
#include "ExtractionGameMode.generated.h"
// 撤离对局的服务器调度中心：地图配置和出战凭据就绪后才进入 Active。
// GameMode 不复制到客户端；需要显示的阶段、截止时间和玩家结果写入 GameState/PlayerState。
// 结算遵循先持久化、后清理角色的顺序，避免客户端看到成功但仓库尚未保存。

UCLASS(Config=Game)
class BROKENEDGE_API AExtractionGameMode : public ABrokenEdgeGameMode
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category="Extraction|Loot")
	TSubclassOf<class AExtractionLootActor> LootActorClass;
	// 配置本模式的状态、控制器和角色类型；延迟开局，等待大厅票据及地图加载校验。
	AExtractionGameMode();
	// 读取人数、时长与出生随机种子；这里只准备参数，不代表对局已经激活。
	virtual void InitGame(const FString& Map, const FString& Options, FString& Error) override;
	// 加载仓库和地图数据，生成战利品与撤离点，并绑定会话结束回调。
	// 出生组或撤离点配置不足时保持阻塞，不能用无效地图启动正式对局。
	virtual void BeginPlay() override;
	// 服务器状态机入口：等待加载与票据 → 激活对局 → 处理倒地、超时和撤离 → 结束会话并回大厅。
	// 仓库不可用时冻结玩家并提前返回，不让未持久化的物资变化继续扩大。
	virtual void Tick(float Delta) override;
	// 连接准入：仅 Waiting 接受新玩家；活动中或已结束的对局返回 MATCH_IN_PROGRESS。
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& Id,
	                      FString& Error) override;
	// 断线时尝试记录 Disconnected，再执行离开清理；世界拆卸阶段跳过仓库访问。
	virtual void Logout(AController* Exiting) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* Player) override;
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual AActor* FindPlayerStart_Implementation(AController* Player, const FString& IncomingName) override;
	// 接入既有死亡回调，交由撤离模式决定进入倒地还是最终死亡。
	virtual void PlayerDefeated(class ABrokenEdgeCharacter* Character, class ABrokenEdgePlayerController* Victim,
	                            ABrokenEdgePlayerController* Attacker) override;
	// 覆写基础复活请求：终态玩家进入观战并销毁旧角色，不生成新的战斗 Pawn。
	virtual void RequestRespawn(ACharacter* Character, AController* Controller) override;
	// 仅活动对局允许有效伤害；过滤无效数值、失能攻击者、友军和守卫之间的伤害。
	virtual float CalculateDamage(AController* Attacker, AController* Victim, float Damage) override;
	// 主动离开按票据与阶段分流：无票据测试角色只清理、未开局返还、已开局按退出结算。
	void LeavePlayer(AController* Player);
	// 伤害作用于独立的倒地生命；降至零时强制最终死亡，返回值表示是否处理了倒地伤害。
	bool DamageDowned(class ABrokenEdgeCharacter* Character, float Damage);
	// 先取消动作；守卫走击杀统计和尸体回收，玩家则依据救援条件选择倒地或持久化死亡。
	// 向同小队其他玩家发送状态提示（倒地、阵亡、撤离）。
	void NotifySquad(const class AExtractionPlayerState* PS, const FString& Text);
	void DefeatCombatant(ABrokenEdgeCharacter* Character, AController* Attacker, bool bFinal = false);
	// 判断是否仍有在线、存活且有 Pawn 的同小队成员；不等同于当前已在救援距离内。
	bool HasRescuer(const class AExtractionPlayerState* Player) const;
	// 服务器检测流血截止时间和救援资格；超时或已无救援者则结束倒地状态。
	void UpdateDowned(double Now);
	// 在服务器按地图配置生成守卫及其库存，具体实现位于 ExtractionGuard.cpp。
	void SpawnGuards();
	// 结局清理守卫及其场景武器，具体实现位于 ExtractionGuard.cpp。
	void RemoveGuards();
	// 等待阶段取消尚未激活的出战记录；活动阶段将剩余成员按超时结算。
	// 只有全部必要写入成功后才发布 Ended，并安排十秒后的会话结束。
	void FinishRound(const FString& Reason);
	// 统一检查活动阶段、玩家存活和仓库可用性；存储故障期间不允许继续有效战斗。
	bool IsCombatActive(AController* Player) const;
	// 将当前枪械弹药捕获到背包后提交仓库结算；返回 false 时调用者不得销毁角色或清空物品。
	// 已离开存活/倒地状态的玩家直接视为处理完毕，持久化层还会按出战成员结果防止重复入库。
	bool SettlePlayer(AController* Player, EExtractionOutcome Outcome);
	// 从 GameInstance 获取服务器仓库子系统；世界销毁期间返回空，防止访问已反初始化的子系统。
	class UExtractionPersistence* Store() const;
	UPROPERTY(EditDefaultsOnly, Config, Category="Extraction|Map")
	TSoftObjectPtr<class UExtractionMapDefinition> MapDefinition;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FExtractionExitFeedbackFixture;
	friend struct FExtractionSpawnLifecycleFixture;
#endif
	// 无缝旅行替换后的旧控制器会暂留在 World 中；仅当前连接所属的撤离控制器可参与出生。
	class AExtractionPlayerState* CurrentPlayerState(APlayerController* Player);
	// 重试尚未生成角色的有效成员，并以同一资格规则统计已就绪人数。
	int32 StartWaitingPlayers();
	UPROPERTY()
	TArray<TObjectPtr<class UExtractionItemDefinition>> ItemDefinitions;
	// 时间字段均使用服务器世界时间（秒）；LoadedAt 控制加载超时，ReturnAt 控制回大厅等待。
	double LoadedAt = 0;
	double RoundSeconds = 1800;
	double ReturnAt = 0;
	int32 ExpectedPlayers = 2;
	bool bTravelRequested = false;
	bool bEndRequested = false;
	bool bEndingPlay = false;
	UPROPERTY()
	TArray<TObjectPtr<class AExtractionExit>> Exits;
	UPROPERTY()
	TObjectPtr<class UExtractionMapDefinition> LoadedMapDefinition;
	// 先检测所在撤离区域，再发布不可用原因或以服务器世界时间推进停留倒计时。
	// 离开区域会清空进度，换撤离点会重新计时；成功写入仓库后才消耗名额、清空背包并转为观战。
	void UpdateExtraction(double Now);
	TSet<TWeakObjectPtr<AActor>> ClaimedStarts;
	TMap<TWeakObjectPtr<AController>, TWeakObjectPtr<AActor>> PlayerStarts;
	// 出生分配仅由服务器维护：小队共享组号、组内使用不同槽位，弱引用不延长 Actor 生命周期。
	TMap<uint8, int32> SquadSpawnGroups;
	TSet<int32> ClaimedSpawnGroups;
	/** 每类出生失败只打一次日志，避免每帧刷屏。 */
	TSet<FString> SpawnAttemptWarnings;
	TMap<uint8, int32> SquadSpawnSlots;
	FRandomStream SpawnRandom;
	bool bMapConfigurationValid = false;
	// 冻结时同时停止移动、射击、装填、切枪和交互；解冻只恢复 Walking 移动模式。
	void FreezePlayer(AController* Player, bool bFreeze);
	// 统计仍在线的存活和倒地成员；倒地玩家仍可能被救援，因此计入未结束人数。
	void RefreshAliveCount();
	// 异步会话结束后的旅行入口；失败时延后两秒重试，标志位阻止重复旅行请求。
	UFUNCTION()
	void SessionEnded(bool bSuccess);
	// 先标记退出以阻断后续 Tick/回调，清理守卫并解绑会话委托，再交给父类销毁。
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
};
