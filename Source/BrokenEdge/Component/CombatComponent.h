// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BrokenEdge/BrokenEdgeTypes/CombatState.h"
#include "BrokenEdge/UI/BrokenEdgeHUD.h"
#include "Components/ActorComponent.h"
#include "CombatComponent.generated.h"


enum class EWeaponType : uint8;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BROKENEDGE_API UCombatComponent : public UActorComponent
{
	GENERATED_BODY()

	friend class ABrokenEdgeCharacter;
	friend class UExtractionCombatComponent;

public:
	// Sets default values for this component's properties
	UCombatComponent();

	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	/**
 * @brief 装备武器
 * @param WeaponToEquip 要装备的武器
 */
	void EquipWeapon(class AWeapon* WeaponToEquip);

	/// 切换武器（主/副武器）
	void SwapWeapons();
	void Reload();
	int32 GetCarriedAmmo() const { return CarriedAmmo; }

	void Drop();
	void FireButtonPressed(bool bPressed);
	void Fire();
	void StartFireTimer();
	void FireTimerFinished();
	void ReloadEmptyWeapon();
	void FireProjectileWeapon();
	void FireHitScanWeapon();

	void FireShotgunWeapon();
	void ShotgunLocalFire(const TArray<FVector_NetQuantize>& TraceHitTargets);

	/**
	 * @brief 服务器端霰弹枪开火
	 * @param TraceHitTargets 多个命中目标
	 * @param FireDelay 开火延迟
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerShotgunFire(const TArray<FVector_NetQuantize>& TraceHitTargets, float FireDelay);

	/**
 * @brief 多播霰弹枪开火效果
 * @param TraceHitTargets 多个命中目标
 */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastShotgunFire(const TArray<FVector_NetQuantize>& TraceHitTargets);


	/**
 * @brief 服务器端开火
 * @param TraceHitTarget 命中目标
 * @param FireDelay 开火延迟
 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerFire(const FVector_NetQuantize& TraceHitTarget, float FireDelay);

	/**
	 * @brief 多播开火效果
	 * @param TraceHitTarget 命中目标
	 */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastFire(const FVector_NetQuantize& TraceHitTarget);

	/**
	 * @brief 完成换弹（蓝图调用）
	 */
	UFUNCTION(BlueprintCallable)
	void FinishReloading();

	UFUNCTION(BlueprintCallable)
	void ShotgunReloadOnce();


	/**
	 * @brief 本地开火效果
	 * @param TraceHitTarget 命中目标
	 */
	void LocalFire(const FVector_NetQuantize& TraceHitTarget);
	bool CanFire();
	void UpdateAmmoValues();

	void UpdateShotgunAmmoValues();
	void JumpToShotgunEnd();

	/**
 * @brief 计算需要装填的弹药数量
 * @return 弹药数量
 */
	int32 AmountToReload();

	/**
 * @brief 完成武器切换（蓝图调用）
 */
	UFUNCTION(BlueprintCallable)
	void FinishSwap();

	/**
	 * @brief 完成武器切换并附加武器（蓝图调用）
	 */
	UFUNCTION(BlueprintCallable)
	void FinishSwapAttachWeapons();


	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	/** 是否正在本地换弹 */
	bool bLocallyReloading = false;

	/**
 * @brief 拾取弹药
 * @param WeaponType 武器类型
 * @param AmmoAmount 弹药数量
 */
	int32 PickupAmmo(EWeaponType WeaponType, int32 AmmoAmount);

protected:
	/**
 * @brief 掉落当前装备的武器
 */
	void DropEquippedWeapon();

	/**
 * @brief 将旗帜附加到左手
 * @param Flag 要附加的旗帜
 */
	void AttachFlagToLeftHand(AActor* Flag);

	/**
 * @brief 将Actor附加到右手
 * @param ActorToAttach 要附加的Actor
 */
	void AttachActorToRightHand(AActor* ActorToAttach);


	/**
	 * @brief 将Actor附加到背包
	 * @param ActorToAttach 要附加的Actor
	 */
	void AttachActorToBackpack(AActor* ActorToAttach);


	/**
 * @brief 装备主武器
 * @param WeaponToEquip 要装备的武器
 */
	void EquipPrimaryWeapon(AWeapon* WeaponToEquip);

	/**
 * @brief 装备副武器
 * @param WeaponToEquip 要装备的武器
 */
	void EquipSecondaryWeapon(AWeapon* WeaponToEquip);

	/**
 * @brief 设置瞄准状态
 * @param bIsAiming 是否瞄准
 */
	void SetAiming(bool bIsAiming);

	/**
 * @brief 服务器端设置瞄准状态
 * @param bIsAiming 是否瞄准
 */
	UFUNCTION(Server, Reliable)
	void ServerSetAiming(bool bIsAiming);

	/**
 * @brief 准星下的射线检测
 * @param TraceHitResult 输出检测结果
 */
	void TraceUnderCrosshairs(FHitResult& TraceHitResult);

	/**
 * @brief 更新HUD准星
 * @param DeltaTime 时间增量
 */
	void SetHUDCrosshairs(float DeltaTime);

	/**
 * @brief 更新携带弹药数量
 */
	void UpdateCarriedAmmo();

	/**
 * @brief 服务器端换弹
 */
	UFUNCTION(Server, Reliable)
	void ServerReload();

	/**
 * @brief 处理换弹逻辑
 */
	void HandleReload();

private:
	/// 关联的角色
	UPROPERTY()
	class ABrokenEdgeCharacter* Character;

	/** 玩家控制器 */
	UPROPERTY()
	class ABrokenEdgePlayerController* Controller;

	/** HUD引用 */
	UPROPERTY()
	class ABrokenEdgeHUD* HUD;

	/** 当前装备的武器 */
	UPROPERTY(ReplicatedUsing = OnRep_EquippedWeapon)
	class AWeapon* EquippedWeapon;

	/** 副武器 */
	UPROPERTY(ReplicatedUsing = OnRep_SecondaryWeapon)
	AWeapon* SecondaryWeapon;

	/**
 * @brief 装备武器复制回调
 */
	UFUNCTION()
	void OnRep_EquippedWeapon();

	/**
	 *
	 * @brief 副武器复制回调
	 */
	UFUNCTION()
	void OnRep_SecondaryWeapon();


	/** 是否正在瞄准 */
	UPROPERTY(ReplicatedUsing = OnRep_Aiming)
	bool bAiming = false;

	UFUNCTION()
	void OnRep_Aiming();

	/** 瞄准按钮是否按下 */
	bool bAimButtonPressed = false;

	/** 基础移动速度 */
	UPROPERTY(EditAnywhere)
	float BaseWalkSpeed;

	/** 开火按钮是否按下 */
	bool bFireButtonPressed;

	/** 是否可以开火,防止玩家连续点击开火大于射速 */
	bool bCanFire = true;

	/** 开火计时器 */
	FTimerHandle FireTimer;

	/** 瞄准移动速度 */
	UPROPERTY(EditAnywhere)
	float AimWalkSpeed;

	/** 命中目标位置 */
	FVector HitTarget;

	/** 准星速度因子 */
	float CrosshairVelocityFactor;

	/** 准星空中因子 */
	float CrosshairInAirFactor;

	/** 准星瞄准因子 */
	float CrosshairAimFactor;

	/** 准星射击因子 */
	float CrosshairShootingFactor;

	/** HUD数据包 */
	FHUDPackage HUDPackage;

	/**
 * @brief 视野插值
 * @param DeltaTime 时间增量
 */
	void InterpFOV(float DeltaTime);

	/** 默认视野 */
	float DefaultFOV;

	/** 当前视野 */
	float CurrentFOV;

	/** 视野插值速度 */
	UPROPERTY(EditAnywhere, Category = Combat)
	float ZoomInterpSpeed = 20.f;

	/** 是否持有旗帜 */
	UPROPERTY(ReplicatedUsing = OnRep_HoldingTheFlag)
	bool bHoldingTheFlag = false;

	/**
    * @brief 持有旗帜状态复制回调
   */
	UFUNCTION()
	void OnRep_HoldingTheFlag();

	/// 战斗状态（带复制通知）
	UPROPERTY(ReplicatedUsing = OnRep_CombatState)
	ECombatState CombatState = ECombatState::ECS_Unoccuiped;

	/**
 * @brief 战斗状态复制回调
 */
	UFUNCTION()
	void OnRep_CombatState();

	/// 旗帜武器（带复制通知）
	UPROPERTY(ReplicatedUsing = OnRep_Flag)
	AWeapon* TheFlag;

	/**
	 * @brief 旗帜复制回调
	 */
	UFUNCTION()
	void OnRep_Flag();

	/** 当前携带的弹药数量 */
	UPROPERTY(ReplicatedUsing = OnRep_CarriedAmmo)
	int32 CarriedAmmo;

	/**
 * @brief 携带弹药复制回调
 */
	UFUNCTION()
	void OnRep_CarriedAmmo();

	/** 各类型武器的携带弹药映射 */
	TMap<EWeaponType, int32> CarriedAmmoMap;

	/** 最大携带弹药量 */
	UPROPERTY(EditAnywhere)
	int32 MaxCarriedAmmo = 500;

	/** 初始突击步枪弹药 */
	UPROPERTY(EditAnywhere)
	int32 StartingARAmmo = 30;

	/** 初始火箭弹药 */
	UPROPERTY(EditAnywhere)
	int32 StartingRocketAmmo = 0;

	/** 初始手枪弹药 */
	UPROPERTY(EditAnywhere)
	int32 StartingPistolAmmo = 0;

	/** 初始冲锋枪弹药 */
	UPROPERTY(EditAnywhere)
	int32 StartingSMGAmmo = 0;

	/** 初始霰弹枪弹药 */
	UPROPERTY(EditAnywhere)
	int32 StartingShotgunAmmo = 0;

	/** 初始狙击枪弹药 */
	UPROPERTY(EditAnywhere)
	int32 StartingSniperAmmo = 0;

	/** 初始手雷数量 */
	UPROPERTY(EditAnywhere)
	int32 StartingGrenadeAmmo = 0;

	/**
	 * @brief 初始化携带弹药
	 */
	void InitializeCarriedAmmo();

public:
	/**
	 * @brief 检查是否应该切换武器
	 * @return 是否应该切换
	 */
	bool ShouldSwapWeapons();
};
