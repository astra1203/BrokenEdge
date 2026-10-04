// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BrokenEdge/BrokenEdgeTypes/WeaponType.h"
#include "GameFramework/Actor.h"
#include "Weapon.generated.h"

enum class EFirstPersonPrimitiveType : uint8;

/**
 * @brief 武器状态枚举
 * @details 定义武器在游戏中的不同状态，用于控制武器行为和显示
 */
UENUM(BlueprintType)
enum class EWeaponState : uint8
{
	EWS_Initial UMETA(DisplayName = "Initial State"), ///< 初始状态
	EWS_Equipped UMETA(DisplayName = "Equipped"), ///< 主武器装备状态
	EWS_EquippedSecondary UMETA(DisplayName = "Equipped Secondary"), ///< 副武器装备状态
	EWS_Dropped UMETA(DisplayName = "Dropped"), ///< 掉落状态
	EWS_MAX UMETA(DisplayName = "DefaultMAX") ///< 默认最大值
};

/**
 * @brief 武器开火方式
 *
 * 决定武器使用即时射线、实体弹丸还是霰弹枪多射线逻辑。
 */
UENUM(BlueprintType)
enum class EFireType : uint8
{
	EFT_HitScan UMETA(DisplayName = "Hit Scan Weapon"), ///< 使用射线检测立即判定命中
	EFT_Projectile UMETA(DisplayName = "Projectile Weapon"), ///< 生成弹丸 Actor 处理飞行和碰撞
	EFT_Shotgun UMETA(DisplayName = "Shotgun Weapon"), ///< 一次发射多条带散射的射线

	EFT_MAX UMETA(DisplayName = "DefaultMAX") ///< 枚举边界值，不作为实际类型使用
};

// enum class EWeaponType;

UCLASS()
class BROKENEDGE_API AWeapon : public AActor
{
	GENERATED_BODY()

public:
	UPROPERTY(Replicated)
	FGuid ExtractionItemId;
	/** Initial actor state travels with the spawn, before any predicted shots. */
	UPROPERTY(ReplicatedUsing=OnRep_ExtractionInitialAmmo)
	int32 ExtractionInitialAmmo = -1;
	UPROPERTY(Replicated)
	FName ExtractionAmmoType = TEXT("Ammo");
	UPROPERTY(Replicated)
	TArray<FName> ExtractionParts;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UExtractionWeaponVisuals> ExtractionVisuals;
	FTransform EquipmentGrip(bool FirstPerson) const;
	FVector EquipmentAimOffset() const;
	UFUNCTION()
	void OnRep_ExtractionInitialAmmo();
	// Sets default values for this actor's properties
	AWeapon();

	// Called every frame
	virtual void Tick(float DeltaTime) override;

	/// 获取需要网络复制的属性列表
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
 * @brief 武器所有者复制完成后的回调，用于刷新所有者缓存和 HUD 弹药。
 */
	virtual void OnRep_Owner() override;
	virtual void SetOwner(AActor* NewOwner) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Local rendering only; preserve each weapon's original world-mesh settings. */
	void RefreshOwnerViewVisibility();
	void RestoreOwnerViewVisibility();

	/**
 * @brief 多播更新弹药数（所有客户端）
 * @param UpdateAmmo 更新后的弹药数
 */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastAmmo(int32 UpdateAmmo);

	/**
	 * 跟新弹药UI
	 */
	void SetHUDAmmo();

	/** 准星中心纹理。 */
	UPROPERTY(EditAnywhere, Category = Crosshairs)
	class UTexture2D* CrosshairsCenter;

	/** 准星左侧纹理。 */
	UPROPERTY(EditAnywhere, Category = Crosshairs)
	UTexture2D* CrosshairsLeft;

	/** 准星右侧纹理。 */
	UPROPERTY(EditAnywhere, Category = Crosshairs)
	UTexture2D* CrosshairsRight;

	/** 准星顶部纹理。 */
	UPROPERTY(EditAnywhere, Category = Crosshairs)
	UTexture2D* CrosshairsTop;

	/** 准星底部纹理。 */
	UPROPERTY(EditAnywhere, Category = Crosshairs)
	UTexture2D* CrosshairsBottom;

	/** 瞄准时的目标视野角。 */
	UPROPERTY(EditAnywhere)
	float ZoomedFOV = 30.f;

	/** 视野角切换时的插值速度。 */
	UPROPERTY(EditAnywhere)
	float ZoomInterpSpeed = 20.f;

protected:
	UFUNCTION()
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	UPROPERTY()
	class ABrokenEdgeCharacter* BrokenEdgeOwnerCharacter;

	/** 缓存的武器所有者玩家控制器。 */
	UPROPERTY()
	class ABrokenEdgePlayerController* BrokenEdgeOwnerController;

	/// 是否使用服务端回退 @TODO
	// UPROPERTY(Replicated, EditAnywhere)
	// bool bUseServerSideRewind = false;

public:
	void EnableCustomDepth(bool bEnable);
	UFUNCTION()
	void OnSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	                          UPrimitiveComponent* OtherComp,
	                          int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnSphereEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	                        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);


	void ShowPickupWidget(bool bShowWidget);

	/**
 * @brief 执行一次开火并播放武器基础表现。
 * @param HitTarget 本次射击瞄准的世界坐标。
 */
	virtual void Fire(const FVector& HitTarget);
	void SpendRound();

	void SetExtractionAmmo(int32 Value)
	{
		Ammo = FMath::Clamp(Value, 0, MagCapacity);
		SetHUDAmmo();
	}

	void UseImmediateServerHitScan() { if (HasAuthority()) bUseServerSideRewind = false; }

	/**
 * @brief 武器掉落时调用
 */
	virtual void Dropped();


	/** 获取武器骨骼网格体组件。 */
	FORCEINLINE USkeletalMeshComponent* GetWeaponMesh() const { return WeaponMesh; }
	EWeaponState GetWeaponState() const { return WeaponState; }

private:
	bool bOwnerViewVisibilityOverridden = false;
	bool bSavedOwnerNoSee = false;
	EFirstPersonPrimitiveType SavedPrimitiveType{};
	TWeakObjectPtr<ABrokenEdgeCharacter> VisibilityOwner;

	/// 武器网格体组件
	UPROPERTY(VisibleAnywhere, Category = "Weapon Properties")
	USkeletalMeshComponent* WeaponMesh;

	/// 拾取检测球体组件
	UPROPERTY(VisibleAnywhere, Category = "Weapon Properties")
	class USphereComponent* AreaSphere;

	/// 武器状态（带复制通知）
	UPROPERTY(ReplicatedUsing = OnRep_WeaponState, VisibleAnywhere, Category = "Weapon Properties")
	EWeaponState WeaponState;

	/**
 * @brief 武器状态复制回调
 */
	UFUNCTION()
	void OnRep_WeaponState();


	/// 拾取提示 Widget
	UPROPERTY(VisibleAnywhere, Category = "Weapon Properties")
	class UWidgetComponent* PickUpWidget;

	/** 开火时在武器网格体上播放的动画。 */
	UPROPERTY(EditAnywhere, Category = "Weapon Properties")
	UAnimationAsset* FireAnimation;

	/** 开火后生成的弹壳 Actor 类型。 */ //@TODO
	// UPROPERTY(EditAnywhere)
	// TSubclassOf<class ACasing> CasingClass;

	/// 当前弹药数
	UPROPERTY(EditAnywhere)
	int32 Ammo;

	/** 弹匣最大容量。 */
	UPROPERTY(EditAnywhere)
	int32 MagCapacity;

	/// 武器类型
	UPROPERTY(EditAnywhere)
	EWeaponType WeaponType;

public:
	/**
 * @brief 设置武器状态
 * @param State 要设置的状态
	 */
	void SetWeaponState(EWeaponState State);

	/**
	 * @brief 武器状态设置时调用
 */
	virtual void OnWeaponStateSet();

	/**
	 * @brief 武器被装备时的处理
 */
	virtual void OnEquipped();

	/// 武器作为副武器装备时调用
	virtual void OnEquippedSecondary();

	/// 武器掉落时调用
	virtual void OnDropped();

	/// 获取当前弹药数
	FORCEINLINE int32 GetAmmo() const { return Ammo; }

	/// 获取武器类型
	FORCEINLINE EWeaponType GetWeaponType() const { return WeaponType; }

	/** 获取弹匣最大容量。 */
	FORCEINLINE int32 GetMagCapacity() const { return MagCapacity; }

	bool IsFull() const;
	void AddAmmo(int32 AmmoToAdd);

	/**
 * @brief 通知所属客户端增加弹药。
 * @param AmmoToAdd 要增加的弹药数量。
 */
	UFUNCTION(Client, Reliable)
	void ClientAddAmmo(int32 AmmoToAdd);


	/**
 * @brief 判断弹匣是否为空。
 * @return 弹药为零时返回 true。
 */
	bool IsEmpty();

	/**
 * @brief 根据散射参数计算实际射线终点。
 * @param HitTarget 未应用散射前的瞄准目标。
 * @return 应用随机散射后的世界坐标。
 */
	FVector TraceEndWithScatter(const FVector& HitTarget);


	/** 当前武器的开火方式。 */
	UPROPERTY(EditAnywhere)
	EFireType FireType;

	/** 是否对射击终点应用随机散射。 */
	UPROPERTY(EditAnywhere, Category = "Weapon Scatter")
	bool bUseScatter = false;

	/** 武器枪口位置到散射球中心的距离。 */
	UPROPERTY(EditAnywhere, Category = "Weapon Scatter")
	float DistanceToSphere = 800.f;

	/** 用于随机选取射击终点的散射球半径。 */
	UPROPERTY(EditAnywhere, Category = "Weapon Scatter")
	float SphereRadius = 75.f;

	/** 两次射击之间的最小时间间隔（秒）。 */
	UPROPERTY(EditAnywhere, Category = Combat)
	float FireDelay = 0.15f;

	/** 是否支持按住开火键连续射击。 */
	UPROPERTY(EditAnywhere, Category = Combat)
	bool bAutomatic = true;


	[[nodiscard]] float GetDamage() const
	{
		return Damage;
	}

	[[nodiscard]] float GetHeadShotDamage() const
	{
		return HeadShotDamage;
	}

protected:
	/** 命中身体时造成的基础伤害。 */
	UPROPERTY(EditAnywhere)
	float Damage = 20.f;

	/** 命中头部时造成的伤害。 */
	UPROPERTY(EditAnywhere)
	float HeadShotDamage = 40.f;

	/** 是否使用服务端倒带进行命中确认。 */
	UPROPERTY(Replicated, EditAnywhere)
	bool bUseServerSideRewind = false;
};
