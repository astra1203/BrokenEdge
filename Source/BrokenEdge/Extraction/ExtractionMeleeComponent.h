#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ExtractionMeleeComponent.generated.h"
// 常驻近战槽独立于持久化枪械槽：Active 由服务器复制，刀模型和蒙太奇在各端按视角呈现。
// 命中使用服务器定时球形扫描；动画资产仅提供表现，原型伤害和时机不等同于源游戏规则。

class USkeletalMesh;

/** Permanent knife slot, independent of persisted firearm slots. Server owns all hit decisions. */
UCLASS()
class BROKENEDGE_API UExtractionMeleeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// 开启复制与逐帧表现刷新，以应对第一人称手臂的销毁和重建。
	UExtractionMeleeComponent();
	// 复制是否选中刀槽；模型组件在本地创建，不依赖复制组件对象。
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	// 每帧维护刀的可见性和挂接，视角切换后不保留失效的手臂父组件。
	virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function) override;
	// 清除命中定时器，恢复曾隐藏的枪械并销毁动态刀组件。
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(Replicated)
	bool Active = false;
	UPROPERTY(EditAnywhere)
	TObjectPtr<USkeletalMesh> Mesh;
	UPROPERTY(EditAnywhere)
	TObjectPtr<class UAnimSequence> Idle;
	UPROPERTY(EditAnywhere)
	TObjectPtr<class UAnimMontage> LightView;
	UPROPERTY(EditAnywhere)
	TObjectPtr<UAnimMontage> HeavyView;
	UPROPERTY(EditAnywhere)
	TObjectPtr<UAnimMontage> LightBody;
	UPROPERTY(EditAnywhere)
	TObjectPtr<UAnimMontage> HeavyBody;
	UPROPERTY(EditAnywhere)
	FTransform HandOffset;
	/** Additional corrections are independent for each skeleton; preserve existing authored offsets. */
	UPROPERTY(EditAnywhere)
	FTransform ViewGripCorrection;
	UPROPERTY(EditAnywhere)
	FTransform BodyGripCorrection;
	/** Local arm/finger transforms sampled from the retargeted third-person knife idle. */
	UPROPERTY(EditAnywhere)
	TMap<FName, FTransform> BodyHoldPose;
	// 失去行动资格时服务器收刀；各端维护枪械隐藏、世界刀与本人专用刀，并重建过期挂接。
	void RefreshPresentation();
	// 销毁第一人称刀组件并置空，允许下次按新的手臂实例重建。
	void ReleaseViewMesh();
	// 同时检查父组件和 RightHand 挂点，单纯组件存在不能证明挂在当前手臂上。
	bool IsViewAttached() const;
	// 服务器检查行动与动作锁，取消枪械输入和医疗，再选择刀或返回枪械。
	UFUNCTION(Server, Reliable)
	void Select(bool Knife, bool SwapGun = false);
	// 服务器设置轻/重击冷却和伤害时刻，再广播动作；命中不依赖动画通知。
	UFUNCTION(Server, Reliable)
	void Attack(bool Heavy);
	// 攻击冷却截止前视为忙碌，防止切槽或重复发起攻击。
	bool Busy() const;

private:
	// 返回持刀角色，所有输入和挂接状态以该角色为上下文。
	class ABrokenEdgeCharacter* Character() const;
	// 沿用撤离战斗资格并要求背包关闭，不能在失能或物品操作中使用近战。
	bool CanAct() const;
	// 伤害时刻再次检查状态，从眼睛沿朝向做球体扫掠并应用点伤害，避免攻击开始后失能仍命中。
	void Strike();
	// 重建第一人称手臂时，按第三人称正在播放的攻击进度和时长比例恢复动作。
	void RestoreViewAttack();
	// 多播播放第三人称与可用的第一人称蒙太奇；不在表现回调中计算伤害。
	UFUNCTION(NetMulticast, Reliable)
	void PlayAttack(bool Heavy);
	// 动态世界模型与本人第一人称模型分开；Transient 表示不作为关卡资源状态保存。
	UPROPERTY(Transient)
	TObjectPtr<class USkeletalMeshComponent> WorldMesh;
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> ViewMesh;
	TWeakObjectPtr<class AWeapon> HiddenGun;
	FTimerHandle StrikeTimer;
	// 服务器攻击冷却截止时刻（秒）；StrikeTimer 控制本次伤害生效时刻，两者不同。
	double NextAttack = 0;
	bool HeavyAttack = false;
};
