#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ExtractionAbilities.generated.h"
// 救援、治疗能力和烟雾使用服务器计时；客户端请求动作，服务器在开始及每次推进时重新验证资格。
// 规则对象通过 Config 从 Game 配置读取，时间单位为秒、距离为厘米；GetDefault 读取类默认规则。

class ABrokenEdgeCharacter;
class USkeletalMeshComponent;

UENUM()
enum class EExtractionAction : uint8 { None, Revive, Heal, Smoke };

// 表现层专用的动作编号：激素枪打队友与自用走不同蒙太奇，但服务器只有一个 Heal 动作。
inline constexpr uint8 ExtractionHealOtherPresentation = 4;

/** Server clocks and tuning shared by gameplay, HUD and smoke visibility. */
UCLASS(Config=Game, DefaultConfig)
class BROKENEDGE_API UExtractionRules : public UObject
{
	GENERATED_BODY()

public:
	// 倒地持续时间与倒地血量控制失能阶段；爬行速度使用厘米/秒。
	UPROPERTY(Config)
	float BleedSeconds = 30;
	UPROPERTY(Config)
	float DownHealth = 100;
	UPROPERTY(Config)
	float CrawlSpeed = 60;
	// 救援需持续施放，范围使用厘米；完成后按 ReviveHealth 恢复生命。
	UPROPERTY(Config)
	float ReviveSeconds = 5;
	UPROPERTY(Config)
	float ReviveRange = 200;
	UPROPERTY(Config)
	float ReviveHealth = 30;
	// 激素枪（官方 C101 技能 20010101）：一发弹，对准队友射出飞镖，否则给自己打；
	// 出手时长取官方 FireInterval 0.66 s。冷却在解包数据里没有，保留项目值。
	// 数据出处见 Docs/Extraction/RestorationEvidence/Stinger/OfficialKit.md。
	UPROPERTY(Config)
	float HealSeconds = .66f;
	/**
	 * 治疗期间的移速倍率：允许边走边治疗，但按此比例降速。
	 * 治疗是唯一允许移动施放的能力，救援与烟雾仍需站定。
	 */
	UPROPERTY(Config, meta=(ClampMin="0.05", ClampMax="1"))
	float HealMoveSpeedScale = .4f;
	UPROPERTY(Config)
	float HealCooldown = 20;
	// 激素飞镖弹道照官方 BulletId 2007：初速 2900 cm/s、无重力、最远 10000 cm、半径 1 cm。
	// 准星射线只用来判断"对准的是不是队友"，命中与否完全由飞镖在服务器上飞过去决定。
	UPROPERTY(Config)
	float StimDartSpeed = 2900;
	UPROPERTY(Config)
	float StimDartMaxDistance = 10000;
	UPROPERTY(Config)
	float StimDartRadius = 1;
	// 激素命中效果，照官方撤离模式 Buff：300013 每 0.1 s +0.2 持续 60 s（2 HP/s）、
	// 300062 每 0.5 s +10 持续 1 s、300100 临时止痛 20 s。重复命中刷新持续时间，不叠加。
	UPROPERTY(Config)
	float StimRegenPerSecond = 2;
	UPROPERTY(Config)
	float StimRegenSeconds = 60;
	UPROPERTY(Config)
	float StimQuickHeal = 10;
	UPROPERTY(Config)
	float StimQuickInterval = .5f;
	UPROPERTY(Config)
	int32 StimQuickTicks = 2;
	/** 止痛期间无视腿部骨折的减速与禁止冲刺；骨折本身仍需夹板处理。 */
	UPROPERTY(Config)
	float StimPainkillerSeconds = 20;
	// 激素枪模型（官方 Support_C101_MedicalGun_01），施放治疗或救援时挂在手上，只是表现。
	// 官方动作驱动 Weapon_RightHand_Joint，枪的 Root 与之重合，所以零偏移挂接；骨骼缺失时回退 RightHand。
	UPROPERTY(Config)
	FSoftObjectPath StimGunViewMesh = FSoftObjectPath(TEXT(
		"/Game/Extraction/Abilities/MedicalGun/1P/Support_C101_MedicalGun_01_1P/SkeletalMeshes/Support_C101_MedicalGun_01_1P.Support_C101_MedicalGun_01_1P"));
	UPROPERTY(Config)
	FSoftObjectPath StimGunWorldMesh = FSoftObjectPath(TEXT(
		"/Game/Extraction/Abilities/MedicalGun/3P/Support_C101_MedicalGun_01_3P/SkeletalMeshes/Support_C101_MedicalGun_01_3P.Support_C101_MedicalGun_01_3P"));
	UPROPERTY(Config)
	FName StimGunBone = TEXT("Weapon_RightHand_Joint");
	// 烟雾投掷的施放、遮挡半径、存续时间和个人冷却参数。出手 0.433 s 取官方 PreFireTime。
	UPROPERTY(Config)
	float SmokeCastSeconds = .4333f;
	// 烟幕半径以厘米计；投掷在视线落点附近，半径决定能遮住多少视野。
	// 150cm = 3 米直径的球形烟团：够挡住一个站着的人，又不会糊住半条街。
	UPROPERTY(Config)
	float SmokeRadius = 150;
	/**
	 * 云团摆几层。P_Clouds 单张云的 alpha 只有两三成，一层就是一片看得穿的雾；
	 * 叠 n 层后不透明度按 1-(1-a)^n 累积，16 层（≈0.98）才读得出"实心烟团"。
	 * 性能吃紧就往下调，8 层大约回到"半透烟雾"的观感。
	 */
	UPROPERTY(Config, meta=(ClampMin="1", ClampMax="16"))
	int32 SmokeCloudLayers = 16;
	/**
	 * 视觉半径相对遮挡半径（SmokeRadius）的倍率。
	 * 1 = 烟正好画满判定球；小于 1 是视觉比判定范围小一圈（宁可少看、不可被判定骗）。
	 * 觉得整团太大就往下调，不用改代码。
	 */
	UPROPERTY(Config, meta=(ClampMin="0.2", ClampMax="2"))
	float SmokeCloudSizeScale = 1.f;
	/**
	 * 垂直尺度倍率：1 = 正球，0.62 = 贴地扁团，4 = 竖起来的烟柱。
	 * 表现和遮挡判定必须用同一个值，否则会出现"看着在烟里、子弹却穿过去"。
	 * 大于 1 时烟团变高，判定椭球跟着变高 —— 这是玩法改动，不只是画面。
	 */
	UPROPERTY(Config, meta=(ClampMin="0.2", ClampMax="6"))
	float SmokeCloudAspect = 4.f;
	UPROPERTY(Config)
	float SmokeSeconds = 18;
	UPROPERTY(Config)
	float SmokeCooldown = 25;
	// 真实投掷：沿准星方向按初速抛出，由重力和弹跳决定落点，不再预先选落点。
	// 官方 WeaponThrowableProjectileTable（ProjectileId 4）：高抛 1800、低抛 900 cm/s，重力倍率 1。
	UPROPERTY(Config)
	float SmokeThrowSpeed = 1800;
	UPROPERTY(Config)
	float SmokeThrowLowSpeed = 900;
	/**
	 * 出手方向相对准星的俯仰修正（度，正数抬高）。官方 ThrowRotationPitchAddition 为 -15，
	 * 但其符号约定没能从数据确认；按"抬高 15°"使用，落点更贴近准星前方，需要时改配置。
	 */
	UPROPERTY(Config)
	float SmokeThrowPitch = 15;
	// 落地到起烟的延迟；延迟让对手有机会抢在成烟前穿过去。
	UPROPERTY(Config)
	float SmokeFuseSeconds = 1.5f;
	// 守卫数量与反应/射击节奏调参；GuardDamageScale 是伤害倍率。
	// 按路线名排序轮流分配，8 个刚好核心区 4 条 + 7F 新区域 4 条各一名。
	UPROPERTY(Config)
	int32 Guards = 8;
	UPROPERTY(Config)
	float GuardReactionSeconds = 2.f;
	/** 0 表示与玩家长按一样使用当前武器 FireDelay；正数只可额外降低射速。 */
	UPROPERTY(Config)
	float GuardShotInterval = .15f;
	/** 默认移速：当前玩家基础步行速度 600 - 75 = 525 cm/s。所有守卫初始化与逐帧更新共用此配置。 */
	UPROPERTY(Config)
	float GuardWalkSpeed = 525.f;
	/** 丢失视野后继续追击/观察的总时限，从最后可见时刻开始，单位秒。 */
	UPROPERTY(Config)
	float GuardPursuitSeconds = 12.f;
	/** 使用最后可见速度推算的位置提前量；不会查询隐藏目标的实时位置。 */
	UPROPERTY(Config)
	float GuardPursuitPredictionSeconds = 1.25f;
	/** 预判位移上限 cm，避免高速目标让追击点飞出地图。 */
	UPROPERTY(Config)
	float GuardPursuitPredictionDistance = 600.f;
	/** 从丢失目标时守卫所在位置到追击点的最大导航路径长度 cm。 */
	UPROPERTY(Config)
	float GuardPursuitMaxDistance = 8000.f;
	/** 搜索移动速度下限 cm/s，实际不低于 GuardWalkSpeed，仍受腿部骨折减速影响。 */
	UPROPERTY(Config)
	float GuardPursuitSpeed = 360.f;
	UPROPERTY(Config)
	float GuardDamageScale = .15f;
	/** 武器本身的散布计算完成后，额外追加的守卫随机圆锥半角。0 表示关闭，单位度。 */
	UPROPERTY(Config, meta=(ClampMin="0", ClampMax="45", Units="deg"))
	float GuardExtraSpreadDegrees = 3.f;
	/** 首次发现目标的视觉距离，单位 cm。 */
	UPROPERTY(Config, meta=(ClampMin="0", Units="cm"))
	float GuardSightRadius = 6000.f;
	/** 已发现目标的保留距离，运行时确保不小于发现距离，减少边界附近反复丢失目标。 */
	UPROPERTY(Config, meta=(ClampMin="0", Units="cm"))
	float GuardLoseSightRadius = 7500.f;
	/** 守卫开局随机配枪（物品 ID，弹种取定义的 DefaultAmmo）；为空或无效时回退 AK。 */
	UPROPERTY(Config)
	TArray<FName> GuardWeapons = {TEXT("AK"), TEXT("M4A1"), TEXT("SCARH")};
	/** 交战时横移换位的平均间隔（秒）；0 表示保持原来的站定射击。 */
	UPROPERTY(Config, meta=(ClampMin="0", Units="s"))
	float GuardStrafeInterval = 3.f;
	/** 横移换位的最大距离，单位 cm。 */
	UPROPERTY(Config, meta=(ClampMin="0", Units="cm"))
	float GuardStrafeDistance = 300.f;
	/** 换弹或刚中弹时寻找掩体的半径，单位 cm；0 表示不找掩体。 */
	UPROPERTY(Config, meta=(ClampMin="0", Units="cm"))
	float GuardCoverRadius = 900.f;
	/** 两次找掩体之间的最短间隔（秒），避免连续中弹时来回跑。 */
	UPROPERTY(Config, meta=(ClampMin="0", Units="s"))
	float GuardCoverCooldown = 6.f;
};

UCLASS()
class BROKENEDGE_API UExtractionAbilities : public UActorComponent
{
	GENERATED_BODY()

public:
	// 启用复制并以 0.05 秒间隔推进能力动作，客户端不自行完成治疗或救援。
	UExtractionAbilities();
	// 动作、目标和救援占用可复制给相关客户端；个人技能冷却仅发给拥有者。
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	// 仅服务器推进：目标失效、移动、下落或资格丢失立即取消，到截止时间才调用 Complete。
	virtual void TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function) override;
	// 有画面的端异步预载激素枪模型。
	virtual void BeginPlay() override;
	// 销毁时释放双向救援关系，再调用父类生命周期清理。
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(ReplicatedUsing=OnRep_Action)
	EExtractionAction Action = EExtractionAction::None;
	/** 动作复制到客户端后同步技能表现蒙太奇；不参与任何结算。 */
	UFUNCTION()
	void OnRep_Action();
	// 服务器绝对完成时刻，0 表示未计时；HUD 应使用同步服务器时间计算剩余值。
	UPROPERTY(Replicated)
	double ActionEnd = 0;
	UPROPERTY(Replicated)
	TObjectPtr<ABrokenEdgeCharacter> Target;
	UPROPERTY(Replicated)
	double HealReadyAt = 0;
	UPROPERTY(Replicated)
	double SmokeReadyAt = 0;
	// 目标身上的救援占用者，同一倒地角色同一时间只允许一名救援者。
	UPROPERTY(Replicated)
	TObjectPtr<ABrokenEdgeCharacter> Rescuer;
	// 递增本地序号并发送动作请求，序号用于服务器拒绝旧消息。
	// bLowThrow 只对烟雾有效：官方蜂巢烟雾支持高抛（X）与低抛（Alt+X）。
	void Request(EExtractionAction Kind, ABrokenEdgeCharacter* Other = nullptr, bool bLowThrow = false);
	// 松开救援输入时发送带新序号的取消，避免旧开始请求覆盖较新的取消。
	void ReleaseRescue();
	// 服务器释放由自己持有的救援锁，清空动作、目标和截止时间，通知复制。
	void Cancel();
	// 受伤时同时取消自己动作和对自己的救援，避免一方已受伤另一方仍继续计时。
	void Damaged();
	// 从 PlayerState 读取倒地状态，不以角色动画或姿势推断。
	bool IsDowned() const;
	// 仅可行动的非守卫角色、空闲战斗状态且背包关闭时允许启动能力。
	bool CanStart() const;
	// 寻找前方可触及的最近倒地队友供本地提示；服务器仍独立验证目标。
	ABrokenEdgeCharacter* RescueFocus() const;
	// 从视点射线选择治疗目标，没命中其他角色时默认自己；友方和血量条件稍后检查。
	ABrokenEdgeCharacter* HealFocus() const;
	// 只有有效且非零的小队编号相同才算友方，未分组的两个角色不会自动成为队友。
	static bool Friendly(const ABrokenEdgeCharacter* A, const ABrokenEdgeCharacter* B);
	// 检查距离与视线，忽略双方角色相关武器；Range 使用厘米。
	static bool Reach(const ABrokenEdgeCharacter* A, const ABrokenEdgeCharacter* B, float Range);
	// 校验序号、动作种类、冷却和目标，再锁定救援者或计算烟雾落点并设定完成时刻。
	UFUNCTION(Server, Reliable)
	void ServerBegin(uint32 Sequence, EExtractionAction Kind, ABrokenEdgeCharacter* Other, bool bLowThrow);
	// 只接受更新序号，取消当前能力并释放对目标的救援占用。
	UFUNCTION(Server, Reliable)
	void ServerCancel(uint32 Sequence);
	/**
	 * 位移是否让当前动作失效。治疗允许边走边施放（移速由 HealMoveSpeedScale 降低），
	 * 救援与烟雾仍需站定：救援要保持在队友范围内，烟雾落点在起手时已经固定。
	 */
	static bool CancelledByMovement(EExtractionAction Kind, bool bMoved);

private:
	// 返回组件所属角色，用于生命状态、距离和动作互斥检查。
	ABrokenEdgeCharacter* Character() const;
	uint32 LocalSequence = 0, LastSequence = 0;
	// 施法起点用于检测位置偏移；LowThrow 记录本次烟雾是高抛还是低抛。
	FVector StartedAt = FVector::ZeroVector;
	bool LowThrow = false;
	// 救援必须是可触及的倒地队友；治疗需要存活、缺血的友方；烟雾没有角色目标。
	bool ValidTarget() const;
	// 在服务器应用救起、回血或生成烟雾；成功作用后记录相应冷却，最后统一释放动作状态。
	void Complete();
	/** 服务器与客户端共用：把当前 Action 映射到角色表现层，None 表示停止。 */
	void RefreshAbilityMontage();
	/**
	 * 各端维护激素枪模型：治疗或救援期间显示并隐藏当前枪械，其余时间收起。
	 * 第一人称手臂可能被重建，每次都检查挂接父组件，不保留失效的挂接。
	 */
	void RefreshStimGun();
	// 本地创建一个挂在 Parent 上的激素枪组件；骨骼缺失时挂 RightHand。
	USkeletalMeshComponent* MakeStimGun(USkeletalMeshComponent* Parent, const FSoftObjectPath& Path, bool bView);
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> WorldStimGun;
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> ViewStimGun;
	TWeakObjectPtr<class AWeapon> HiddenGun;
	// 持有异步加载句柄，保证模型在角色存续期间不被回收。
	TSharedPtr<struct FStreamableHandle> StimGunLoad;
};
