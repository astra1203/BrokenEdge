#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ExtractionAbilities.generated.h"
// 救援、治疗能力和烟雾使用服务器计时；客户端请求动作，服务器在开始及每次推进时重新验证资格。
// 规则对象通过 Config 从 Game 配置读取，时间单位为秒、距离为厘米；GetDefault 读取类默认规则。

class ABrokenEdgeCharacter;

UENUM()
enum class EExtractionAction : uint8 { None, Revive, Heal, Smoke };

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
	// 治疗能力的施放时长、范围、恢复量及冷却，与背包医疗物品各自计时。
	UPROPERTY(Config)
	float HealSeconds = 2;
	UPROPERTY(Config)
	float HealRange = 1000;
	UPROPERTY(Config)
	float HealAmount = 50;
	UPROPERTY(Config)
	float HealCooldown = 20;
	// 烟雾投掷的施放、距离、遮挡半径、存续时间和个人冷却参数。
	UPROPERTY(Config)
	float SmokeCastSeconds = .5f;
	UPROPERTY(Config)
	float SmokeRange = 1500;
	UPROPERTY(Config)
	float SmokeRadius = 400;
	UPROPERTY(Config)
	float SmokeSeconds = 15;
	UPROPERTY(Config)
	float SmokeCooldown = 30;
	// 守卫数量与反应/射击节奏调参；GuardDamageScale 是伤害倍率。
	UPROPERTY(Config)
	int32 Guards = 4;
	UPROPERTY(Config)
	float GuardReactionSeconds = 2.f;
	UPROPERTY(Config)
	float GuardShotInterval = .8f;
	UPROPERTY(Config)
	float GuardDamageScale = .3f;
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
	// 销毁时释放双向救援关系，再调用父类生命周期清理。
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(Replicated)
	EExtractionAction Action = EExtractionAction::None;
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
	void Request(EExtractionAction Kind, ABrokenEdgeCharacter* Other = nullptr);
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
	void ServerBegin(uint32 Sequence, EExtractionAction Kind, ABrokenEdgeCharacter* Other);
	// 只接受更新序号，取消当前能力并释放对目标的救援占用。
	UFUNCTION(Server, Reliable)
	void ServerCancel(uint32 Sequence);

private:
	// 返回组件所属角色，用于生命状态、距离和动作互斥检查。
	ABrokenEdgeCharacter* Character() const;
	uint32 LocalSequence = 0, LastSequence = 0;
	// 施法起点用于检测位置偏移；SmokePoint 是开始施法时选定的世界落点。
	FVector StartedAt = FVector::ZeroVector;
	FVector SmokePoint = FVector::ZeroVector;
	// 救援必须是可触及的倒地队友；治疗需要存活、缺血的友方；烟雾没有角色目标。
	bool ValidTarget() const;
	// 在服务器应用救起、回血或生成烟雾；成功作用后记录相应冷却，最后统一释放动作状态。
	void Complete();
};
