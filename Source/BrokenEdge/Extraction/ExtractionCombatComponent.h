#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BrokenEdge/Weapon/WeaponMechanics.h"
#include "BrokenEdge/Weapon/WeaponSprintState.h"
#include "ExtractionCombatComponent.generated.h"
// 撤离模式专用战斗协议：拥有者预测表现，服务器校验射击与装填并维护弹药，其他客户端只播放效果。
// Server/Client/NetMulticast RPC 分别对应请求、拥有者回执和远端表现；Reliable 不等于免除服务器参数校验。
// 权威装填和切枪使用定时器，不依赖专服是否渲染动画或触发 AnimNotify。

class AWeapon;
class ABrokenEdgeCharacter;

/** Mode-scoped authority and owner prediction. Legacy weapons keep their existing protocol. */
UCLASS()
class BROKENEDGE_API UExtractionCombatComponent : public UActorComponent
{
	GENERATED_BODY()
	friend class FExtractionDriver;

public:
	// 启用复制与逐帧更新，服务器规则和本地预测共用同一个组件类型。
	UExtractionCombatComponent();
	// 通过当前 GameState 判断是否启用撤离协议，其他模式保持原战斗入口。
	bool IsEnabled() const;
	// 要求活动对局、存活或守卫身份、未失能且未攀爬；这是多种战斗交互共用的前置条件。
	bool CanAct() const;
	// 本地验证基础条件，非权威拥有者先预测开火并登记待确认射击，再向服务器发送目标。
	void RequestShot(const FVector& Target);
	// 服务器 AI 走同一射击请求校验链路，不能绕过弹药和射速规则。
	void FireGuard(const FVector& Target);
	// 拥有者先播放装填表现并请求服务器；此时不真正从背包转移弹药。
	void RequestReload();
	// 服务器重验武器归属和动作条件，记录目标武器并按空仓/普通装填时长安排完成回调。
	void BeginServerReload();
	// 清除定时器与弱引用，退出装填状态并撤销拥有者预测标记；取消本身不补弹。
	void CancelReload();
	// 记录原主副武器，用参数或通知时刻确定挂接和结束时间；广播蒙太奇后由服务器定时器推进。
	void BeginServerSwap();
	// 取消挂接/结束计时器并清空快照；已完成的挂接不会被此函数反向交换。
	void CancelSwap();
	// 服务器装备同步时设置弹药，并通过同一回执通道更新客户端，防止旧预测覆盖新装备状态。
	void EquipmentAmmo(AWeapon* Weapon, int32 Ammo);
	// 销毁前清除镜头临时偏移和定时器，防止后续回调访问失效角色。
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	// 补齐当前武器配置版本并转发到版本化 RPC；旧调用入口仍接受同一套服务器检查。
	// ViewTime 是射手开枪时屏幕上画面对应的服务器时间，小于 0 表示不做延迟补偿（AI、主机本地玩家）。
	void ServerRequestShot(uint32 ShotId, AWeapon* Weapon, FVector_NetQuantize Target, float ViewTime = -1.f);
	// 按序号去重，核验武器归属、装配版本、瞄准方向、射速、弹药和动作互斥，再执行服务端命中。
	// 无论接受或拒绝都给拥有者回执，但绝不回传伪造请求指定的他人物品状态。
	UFUNCTION(Server, Reliable)
	void ServerRequestShotVersioned(uint32 ShotId, AWeapon* Weapon, FVector_NetQuantize Target, uint32 Revision,
	                                float ViewTime);
	// 服务器按客户端报告的画面时间计算倒带时长，并限制在倒带上限和该玩家实测延迟之内，防止伪造时间。
	float ServerRewindFor(float ViewTime) const;
	// 旧丢枪入口也转入背包事务，防止仅销毁 Actor 而保留可再次使用的物品。
	UFUNCTION(Server, Reliable)
	void ServerDropWeapon();
	uint32 GetAcceptedShots() const { return AcceptedShots; }
	uint32 GetRejectedShots() const { return RejectedShots; }
#if WITH_DEV_AUTOMATION_TESTS
	// Preserve authoritative shot times across slower driver snapshots; never replicated or saved.
	const TArray<TPair<uint32, double>>& GetTestShotTimes() const { return TestShotTimes; }
#endif
	// 推进开镜插值、屏息消耗恢复、散布回落和后座偿还；本地额外更新镜头表现及自动连射。
	virtual void TickComponent(float Delta, ELevelTick Kind, FActorComponentTickFunction* Tick) override;
	// 随机种子只同步给拥有者，使本地预测与服务器用相同射击 ID 构造散布。
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// 有有效参数时检查冲刺和举枪/跑射延迟；无参数时允许既有逻辑继续判断。
	bool CanFireMechanics() const;
	// 开火意图与射击请求走同一可靠通道；服务器也必须先退出冲刺再接受首发。
	bool SetFireInputHeld(bool bHeld);
	bool IsFireInputHeld() const { return SprintFire.FireHeld; }
	bool CanSprintWithWeapon() const;
	void OnSprintEnded();
	void CancelFireInput();
	UFUNCTION(Server, Reliable)
	void ServerSetFireInputHeld(bool bHeld);
	const FResolvedWeaponStats& GetWeaponStats() const { return Stats; }
	float GetAimAlpha() const { return AimAlpha; }
	FVector2D GetCrosshairAngularSpread() const;
	float GetBreath() const { return Breath; }
	double GetBreathResumeTime() const { return BreathResume; }
	FRotator GetVisualKick() const { return FRotator(-VisualKick, 0, 0); }
	// 本地检查开镜资格并发出屏息请求；服务器仍会重新校验当前状态。
	void HoldBreathPressed();
	// 本地和服务器同时解除屏息输入，资源恢复仍按 Tick 规则执行。
	void HoldBreathReleased();
	// 服务器只接受当前可稳定瞄准且已开镜的屏息请求。
	UFUNCTION(Server, Reliable)
	void ServerHoldBreath(bool bHeld);

private:
	// 组件所属 Actor 应为 BrokenEdgeCharacter，转换失败时返回空指针。
	ABrokenEdgeCharacter* Character() const;
	FResolvedWeaponStats Stats;
	FWeaponSprintState SprintFire;
	bool ApplyFireInput(bool bHeld);
	TWeakObjectPtr<AWeapon> MechanicsWeapon;
	TArray<FName> MechanicsParts;
	uint32 Configuration = 0;
	FVector2D SpreadAccumulation = FVector2D::ZeroVector;
	FVector2D RecoilDebt = FVector2D::ZeroVector;
	FRotator PreviousControl = FRotator::ZeroRotator;
	bool bHaveControl = false;
	// AimAlpha 为 0..1 开镜权重，Breath 为 0..100 资源；Resume/ReadyAt 是秒制时间门槛。
	float AimAlpha = 0, Breath = 100, BreathResume = 0, VisualKick = 0;
	double LastMechanicsShot = -100, ReadyAt = 0;
	uint32 BurstIndex = 0;
	bool bBreathHeld = false;
	// 清除开镜、连射、散布与镜头冲量，不重置屏息余量及冷却，避免切枪刷资源。
	void ResetTransientMechanics();
	// 只有能行动、非冲刺持刀、未开背包且未装填时，才允许稳定瞄准相关状态。
	bool CanSteadyWeapon() const;
	UPROPERTY(Replicated)
	uint32 MechanicsSeed = 0;
	// 武器、附件或配置版本变化时重新解析参数并重置短期枪械状态；切枪不能补满玩家屏息资源。
	void RefreshMechanics();
	// 以共享种子和射击 ID 计算散布，叠加姿态与屏息影响，再推进连射、后座和资源状态。
	// bApplyInput 只控制是否给本地控制旋转施加后座，镜头附加冲量与权威瞄准射线分开。
	FVector ResolveShot(const FVector& Target, uint32 Id, bool bApplyInput);
	// 本地发射序号、服务器处理水位和客户端确认水位分别维护，不把网络到达顺序当作开火资格。
	uint32 LocalShotId = 0;
	uint32 LastProcessedShot = 0;
	uint32 LastAcknowledgedShot = 0;
	uint32 AcceptedShots = 0;
	uint32 RejectedShots = 0;
#if WITH_DEV_AUTOMATION_TESTS
	TArray<TPair<uint32, double>> TestShotTimes;
#endif
	double NextShotTime = 0;
	// 待确认预测按武器记录；弱引用避免网络回执延长已销毁武器的生命周期。
	TMap<uint32, TWeakObjectPtr<AWeapon>> PendingShots;
	TWeakObjectPtr<AWeapon> ReloadWeapon;
	FTimerHandle ReloadTimer;
	FTimerHandle SwapAttachTimer, SwapFinishTimer;
	TWeakObjectPtr<AWeapon> SwapPrimary, SwapSecondary;
	// 挂接时再检查两把枪与开始时一致，避免期间丢弃或更换后把过期对象重新挂回。
	void CommitServerSwap();
	// 结束计时后统一进入切枪清理，解除动作锁并恢复副武器轮廓。
	void FinishServerSwap();
	// 到时再次确认仍持有原武器且处于装填状态，再把背包子弹转入弹匣并同步回执。
	void FinishServerReload();
	// 在各端播放切枪动作，表现通知不承担服务器最终状态提交。
	UFUNCTION(NetMulticast, Reliable)
	void MulticastSwapStarted();
	// 清除已确认序号及更早请求，用服务器弹药减去剩余预测发数恢复本地显示。
	UFUNCTION(Client, Reliable)
	void ClientShotResult(uint32 ShotId, AWeapon* Weapon, int32 Ammo, bool bAccepted);
	// 拥有者收到服务器枪械状态后叠加尚未确认的本地射击，避免校正抹掉后续预测。
	UFUNCTION(Client, Reliable)
	void ClientMechanicsResult(uint32 ShotId, AWeapon* Weapon, uint32 Sequence, FVector2D Spread, float InBreath,
	                           double ServerTime, double ShotTime);
	// 仅远端非拥有者播放射击；服务器已执行命中、拥有者已预测，二者不能再重复播放或扣弹。
	UFUNCTION(NetMulticast, Reliable)
	void MulticastShotEffects(AWeapon* Weapon, FVector_NetQuantize Target, int32 Ammo);
	// 在客户端应用服务器弹药快照，与射击回执保持同组件可靠通道的顺序。
	UFUNCTION(NetMulticast, Reliable)
	void MulticastReloadAmmo(AWeapon* Weapon, int32 Ammo);
	// 撤销本地装填预测标记；真实弹药以服务器快照为准。
	UFUNCTION(Client, Reliable)
	void ClientReloadRejected();
};
