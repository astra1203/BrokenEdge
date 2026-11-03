#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ExtractionItems.h"
#include "ExtractionHealth.generated.h"
// 当前角色的创伤组件：处理弹药倍率、护甲吸收、出血与腿部骨折；护甲耐久仍保存在物品实例。
// 命中由服务器执行，复制只传创伤状态；死亡/倒地的最终分流交给 ExtractionGameMode。

UCLASS(Config=Game, DefaultConfig)
class BROKENEDGE_API UExtractionEquipmentRules : public UObject
{
	GENERATED_BODY()

public:
	// 一次命中实际生命损失达到该阈值才施加创伤；不同于命中输入伤害。
	UPROPERTY(Config)
	float TraumaThreshold = 15;
	UPROPERTY(Config)
	float BleedingInterval = 2;
	UPROPERTY(Config)
	float BleedingDamage = 1;
	UPROPERTY(Config)
	float FractureSpeedScale = .7f;
	UPROPERTY(Config)
	float StoppedAbsorption = .6f;
	UPROPERTY(Config)
	float PenetratedAbsorption = .2f;
};

/** Trauma belongs to the current pawn; durable equipment belongs to item instances. */
UCLASS()
class BROKENEDGE_API UExtractionHealth : public UActorComponent
{
	GENERATED_BODY()

public:
	// 启用复制并每 0.1 秒检查出血时间门槛。
	UExtractionHealth();
	// 同步出血与骨折布尔状态，供 UI 和角色表现读取。
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	// 服务器按间隔直接扣出血生命，不再经过护甲也不重新施加创伤；不能行动时推迟下一次扣血。
	virtual void TickComponent(float Delta, ELevelTick Kind, FActorComponentTickFunction* Tick) override;
	UPROPERTY(Replicated)
	bool Bleeding = false;
	UPROPERTY(Replicated)
	bool LegFracture = false;
	// 激素止痛中：骨折仍在，但减速与禁止冲刺暂时失效；由服务器到时清除。
	UPROPERTY(Replicated)
	bool Painkiller = false;
	// 骨折当前是否真正影响移动，角色移速和冲刺都读这里。
	bool FractureHinders() const { return LegFracture && !Painkiller; }
	// 服务器施加激素效果：持续回血、快速回血和止痛，重复施加只刷新时间不叠加。
	void ApplyStim();
	// 立即结束全部激素效果（测试夹具重置基线用）。
	void ClearStim();
	// 服务器先过滤友军等无效伤害，再应用弹药/守卫倍率、护甲和生命损失，最后依据实际掉血产生创伤。
	void Bullet(float Damage, FName Bone, FName Ammo, AController* Attacker, AActor* Causer);
	// 爆炸伤害：不经过弹药/护甲部位计算，按官方爆炸主要伤及四肢的特点只扣生命并可能造成出血。
	void Explosion(float Damage, AController* Attacker, AActor* Causer);
	// 最近一次实际掉血中弹的服务器时间，守卫据此决定是否找掩体；-1 表示本局未中弹。
	double LastBulletAt = -1;
	// 按治疗类型检查是否缺血、出血或骨折，供医疗开始与完成时复核。
	bool Needs(EExtractionMedicalEffect Effect) const;
	// 服务器应用对应治疗并复制结果；返回 false 时上层不应消耗物品。
	bool Treat(const UExtractionItemDefinition* Definition);
	// 救起时清除出血与计时；这里没有清除骨折，避免注释误写为恢复全部创伤。
	void Revived();
	// 头部映射头盔槽 2，骨盆/脊柱映射护甲槽 3；其余部位返回 -1 不套用护甲。
	static int32 ArmorSlotForBone(FName Bone);
	// 兼容项目骨架的腿部骨名，用于决定有效创伤是否附带骨折。
	static bool IsLeg(FName Bone);
	// 按穿透等级选择吸收率，并受剩余耐久限制；引用参数扣耐久，返回未被吸收的伤害。
	static float Absorb(float Damage, int32 Penetration, int32 ArmorLevel, float& Durability);
#if WITH_DEV_AUTOMATION_TESTS
	int32 TestBulletSequence = 0;
	FName TestHitBone;
	float TestHealthLoss = 0;
#endif

private:
	// 下一次出血的世界时间（秒）；BleedInstigator 弱引用保留伤害归属而不阻止控制器销毁。
	double NextBleed = 0;
	TWeakObjectPtr<AController> BleedInstigator;
	// 激素计时（服务器世界时间）：持续回血截止、下一跳快速回血时刻与剩余跳数、止痛截止。
	double RegenEnds = 0;
	double NextQuickHeal = 0;
	int32 QuickHealsLeft = 0;
	double PainkillerEnds = 0;
	// 推进激素效果；返回是否改动了生命或状态。
	bool TickStim(float Delta);
	// 获取当前受伤角色，组件不管理跨关卡的账号生命状态。
	class ABrokenEdgeCharacter* Character() const;
};
