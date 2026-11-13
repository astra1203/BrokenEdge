#pragma once

#include "CoreMinimal.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "ExtractionItems.h"
#include "ExtractionPlayerController.generated.h"
// 撤离控制器把本地输入转为服务器 RPC，并管理 HUD、背包、观战和离开会话。
// 本地界面只读复制状态；物品结果、撤离成功和观战资格由服务器决定。HUD 实现位于 ExtractionWidget。

/** 阵亡回顾：服务器按本局受到的伤害记录，阵亡时发给本人展示。 */
USTRUCT()
struct FExtractionDeathRecap
{
	GENERATED_BODY()
	/** 最后一次造成伤害的来源名称（玩家名或“守卫”）。 */
	UPROPERTY() FString Killer;
	UPROPERTY() FString Weapon;
	/** 最后一次受击时与攻击者的水平距离（米）。 */
	UPROPERTY() float Distance = 0;
	/** 本局受击次数与总伤害。 */
	UPROPERTY() int32 Hits = 0;
	UPROPERTY() float Damage = 0;
	/** 倒地后流血耗尽而阵亡。 */
	UPROPERTY() bool bBledOut = false;
};

UCLASS()
class BROKENEDGE_API AExtractionPlayerController : public ABrokenEdgePlayerController
{
	GENERATED_BODY()
	friend class FExtractionDriver;

public:
	UPROPERTY(EditDefaultsOnly, Category="Extraction|UI")
	TSoftClassPtr<class UExtractionWidget> ExtractionWidgetClass = TSoftClassPtr<UExtractionWidget>(
		FSoftObjectPath(TEXT("/Game/UI/Extraction/WBP_ExtractionHUD.WBP_ExtractionHUD_C")));
	virtual void BeginPlay() override;
	virtual void ReceivedPlayer() override;
	virtual void PostSeamlessTravel() override;
	/** Designer-authored inventory; soft class reference is collected for cooking. */
	UPROPERTY(EditDefaultsOnly, Category="Extraction|UI")
	TSoftClassPtr<class ULootingMainView> InventoryWidgetClass = TSoftClassPtr<ULootingMainView>(
		FSoftObjectPath(TEXT("/Game/UI/Looting/WBP_LootingPickupView_Pc.WBP_LootingPickupView_Pc_C")));
	// 服务器修复失效观战目标；本地刷新界面、切输入模式，再维护避障观战相机。
	// 目标区域尚未可见时保留旧视点，避免相机提前进入未加载场景。
	virtual void Tick(float Delta) override;
	// 远端观战连接在服务器的相关性视点跟随所选队友，避免仍用撤离点附近的旧相机位置。
	virtual void GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation) const override;
	// 在父类输入之上绑定菜单、背包、地图、医疗、能力和武器槽；E 松开用于停止救援。
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	// Cancel held gestures without interpreting cancellation as a release/throw/tap.
	void StopHeldActions();
	void HandleApplicationActivation(bool bActive);
	// 移除本地 UI、观战相机、重试定时器和会话委托，防止关卡切换后仍收到旧回调。
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	// 观战目标与观战标记仅同步给控制器拥有者，其他玩家不需要该私有视角状态。
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	// 服务器保存当前视点、解除 Pawn 控制并切换观战状态，再选择合法队友目标。
	void BeginExtractionSpectating();
	// 服务器只接受观战者的 +1/-1 切换请求，拒绝任意方向和仍存活玩家。
	UFUNCTION(Server, Reliable)
	void ServerCycleSpectator(int32 Direction);
	// 服务器用 bLeaving 防止重复离开，调用 GameMode 离局处理后通知本机清理会话。
	UFUNCTION(Server, Reliable)
	void ServerLeaveExtraction();
	UPROPERTY(Replicated)
	TObjectPtr<class AExtractionPlayerState> SpectatedPlayer;
	UPROPERTY(Replicated)
	bool bExtractionSpectating = false;
	bool IsMenuOpen() const { return bMenuOpen; }
	// Esc 按优先级关闭地图、取消拖动、关闭背包或取消治疗，均无待处理操作时才切换对局菜单。
	void ToggleExtractionMenu();
	// 通过当前 Pawn 获取交互组件，观战没有 Pawn 时返回空。
	class UExtractionInteractionComponent* Interaction() const;
	// 交互优先救援，其次钥匙门、战利品；没有目标时才尝试切枪，避免同一按键触发多种动作。
	void Interact();
	/** F 键：只处理钥匙房门（不换枪、不抢救人）。门由服务器校验钥匙卡并扣耐久。 */
	void InteractDoor();
	UFUNCTION(Server, Reliable)
	void ServerToggleDoor(class AExtractionDoor* Door);
	// 先确认门对象有效且属于同一世界，再由门执行完整权限和库存版本校验。
	UFUNCTION(Server, Reliable)
	void ServerUnlockDoor(class AExtractionKeyRoom* Door, int32 InventoryRevision);
	// G / Alt+G：按下进入瞄准（HUD 画预计轨迹与落点），松开 G 投出；服务器扣除物品并生成投掷物。
	void ThrowGrenade();
	void ThrowGrenadeLow();
	/** 本地手雷瞄准状态：按住 G 期间为真，Low 表示按下时带 Alt（低抛）。 */
	bool bAimingGrenade = false;
	bool bAimingGrenadeLow = false;
	UFUNCTION(Server, Reliable)
	void ServerThrowGrenade(bool bLow);
	double NextGrenadeAt = 0;
	// 鼠标中键：在准星指向处标点，服务器转发给同小队的所有玩家（含自己）。
	void PlacePing();
	UFUNCTION(Server, Unreliable)
	void ServerPing(FVector_NetQuantize Location);
	UFUNCTION(Client, Reliable)
	void ClientPing(FVector_NetQuantize Location, const FString& Sender, bool bSelf);
	double NextPingAt = 0;
	struct FPing
	{
		double At = 0;
		FVector Location = FVector::ZeroVector;
		FString Sender;
		bool bSelf = false;
	};
	/** 客户端当前显示的标点；每人只保留最新一个。 */
	TArray<FPing> Pings;
	static constexpr double PingSeconds = 8.0;
	// 电闸由服务器再次校验距离、视线与行动能力。
	UFUNCTION(Server, Reliable)
	void ServerPullSwitch(class AExtractionExitSwitch* Switch);
	// 滑索由服务器再次校验距离后开始骑乘。
	UFUNCTION(Server, Reliable)
	void ServerUseZipline(class AExtractionZipline* Line);
	// 释放交互键时取消救援请求，不直接修改目标生命或进度。
	void ReleaseInteract();
	// 选择本地治疗焦点后发请求，服务器仍检查友方、距离和血量。
	void HealAbility();
	// V：轻点切出/收起激素枪，长按（HealHoldSeconds）给自己打针。
	void HealReleased();
	double HealDownAt = -1;
	bool bHealHoldUsed = false;
	bool bInputWasActionable = false;
	static constexpr double HealHoldSeconds = .35;
	// 发起烟雾能力请求，落点与冷却在服务器处理。
	void SmokeAbility();
	void SmokeAbilityLow();
	// 仅可行动且未开菜单时切换本人背包，不绕过服务器 Open 流程。
	void ToggleInventory();
	// 切换只读战术地图，必要时关闭背包；HitTestInvisible 使地图不截获游戏鼠标输入。
	void ToggleTacticalMap();
	// 菜单未打开时请求服务器选择刀槽；实际资格由近战组件检查。
	void SelectKnife();
	// 请求退出刀槽回到当前枪，不额外交换主副武器。
	void SelectHeldGun();
	// 请求退出刀槽并切换另一把枪，仍受服务器动作锁约束。
	void SelectOtherGun();
	void SelectWeaponSlot(int32 Slot);
	void SelectPrimaryGun();
	void SelectSecondaryGun();
	void ToggleProne();
	// 快捷医疗只选择本人背包的 Medkit；正在治疗时同一输入改为取消。
	void UseMedical();
	// 把手持槽物品转换为 Drop 请求，复用背包事务而非直接销毁武器 Actor。
	void DropEquippedItem();
	// 以本地和复制会话号的较大值递增发请求，打开前先停持续射击，避免延迟期间继续预测。
	void RequestOpenInventory(class AExtractionLootActor* Target, bool bOpen);
	// 统一填入递增请求序号、当前交互会话和两侧容器版本，再发送物品 RPC。
	void RequestItem(FExtractionItemRequest Request);
	// 服务器交由交互组件验证会话与目标，随后把操作反馈发回所属客户端。
	UFUNCTION(Server, Reliable)
	void ServerOpenInventory(uint32 Session, class AExtractionLootActor* Target, bool bOpen);
	// 服务器将请求交给 Execute 事务入口，回执包含该序号与结果文本。
	UFUNCTION(Server, Reliable)
	void ServerItemOperation(FExtractionItemRequest Request);
	// 取消服务器医疗计时，不在客户端提前消耗或恢复物品。
	UFUNCTION(Server, Reliable)
	void ServerCancelMedical();
	// 服务器检查行动与背包状态，取消医疗后再调用既有切枪入口。
	UFUNCTION(Server, Reliable)
	void ServerSwapExtractionWeapons();
	// 拥有者记录结果消息和已回执水位；库存本身仍等待属性复制更新。
	UFUNCTION(Client, Reliable)
	void ClientItemResult(uint32 Sequence, const FString& Error);
	// 服务器销毁控制器时触发退出掉落清理，防重复由背包组件维护。
	virtual void Destroyed() override;
	FString ItemMessage;
	uint32 ItemResultSequence = 0;
	/** 一次性提示（开门结果、物品错误等）的展示时长，到点自动清空。 */
	static constexpr float ItemMessageSeconds = 3.f;
	/** 当前提示的过期世界时间；0 表示还没有计时，交给 Tick 按文本变化补上。 */
	double ItemMessageUntil = 0.0;
	/** 设置一条会自动消失的提示；重复触发会重新计时。 */
	void ShowItemMessage(const FString& Message, float Seconds = ItemMessageSeconds);

	// 战斗反馈：只用于本地 HUD 表现，不参与任何结算。
	/** 命中结果：0 命中、1 击倒、2 击杀。 */
	UFUNCTION(Client, Unreliable)
	void ClientCombatHit(uint8 Result, const FString& Victim);
	/** 本人受到伤害，From 为伤害来源的世界坐标。 */
	UFUNCTION(Client, Unreliable)
	void ClientDamaged(FVector_NetQuantize From);
	/** 队友状态变化（倒地、阵亡、撤离）的提示，与击杀提示同列显示。 */
	UFUNCTION(Client, Reliable)
	void ClientSquadNotice(const FString& Text);
	/** 最近一次命中的本地时间与结果，HUD 据此绘制命中标记。 */
	double LastHitAt = -100;
	uint8 LastHitResult = 0;
	/** 近期受击来源（本地时间, 世界坐标）。 */
	TArray<TPair<double, FVector>> DamageSources;
	/** 近期击倒/击杀提示（本地时间, 文本）。 */
	TArray<TPair<double, FString>> CombatNotices;
	// 服务器：记录一次受击，供阵亡回顾使用。
	void RecordDamage(const FString& Killer, const FString& Weapon, float Distance, float Damage);
	// 服务器：本人最终阵亡时调用，把回顾发给客户端。
	void SendDeathRecap(bool bBledOut);
	UFUNCTION(Client, Reliable)
	void ClientDeathRecap(const FExtractionDeathRecap& Recap);
	/** 客户端收到的阵亡回顾与收到时刻；未阵亡时 ReceivedAt 为负。 */
	FExtractionDeathRecap DeathRecap;
	double DeathRecapAt = -1;

private:
	void EnsureLocalWidgets();
	bool bLocalWidgetsAttempted = false;
	UPROPERTY(Transient)
	TObjectPtr<class UExtractionTacticalMap> TacticalMap;
	bool bMenuOpen = false;
	bool bLeaving = false;
	uint32 LocalItemSequence = 0;
	// 每次背包开关使用新会话，避免旧容器请求作用到后来打开的容器。
	uint32 LocalInventorySession = 0;
	/** 上次参与计时的提示文本，用于捕捉界面里直接赋值 ItemMessage 的情况。 */
	FString TimedItemMessage;
	UPROPERTY(Transient)
	TObjectPtr<class ULootingMainView> InventoryWidget;
	FTimerHandle LeaveRetryTimer;
	// 无会话直接返回，有异步操作时延迟重试，否则绑定销毁回调后发请求。
	void DestroyLocalSession();
	UPROPERTY(Transient)
	TObjectPtr<class UExtractionWidget> ExtractionWidget;
	UPROPERTY(Transient)
	TObjectPtr<class ACameraActor> SpectatorCamera;
	FVector LastSpectatorLocation = FVector::ZeroVector;
	FRotator LastSpectatorRotation = FRotator::ZeroRotator;
	// 仅在在线、存活、有 Pawn 的同小队成员中按玩家 ID 排序循环选择，不能观战敌人。
	void SelectSpectatorTarget(int32 Direction);
	// 所属客户端开始销毁本机会话，再返回大厅地图。
	UFUNCTION(Client, Reliable)
	void ClientLeaveExtraction();
	// 解除回调绑定；失败延迟重试，成功后打开大厅，避免遗留旧会话。
	UFUNCTION()
	void LocalSessionDestroyed(bool bSuccess);
};
