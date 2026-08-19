// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BrokenEdge/BrokenEdgeTypes/WeaponType.h"
#include "GameFramework/Actor.h"
#include "Weapon.generated.h"

/**
 * @brief 武器状态枚举
 * @details 定义武器在游戏中的不同状态，用于控制武器行为和显示
 */
UENUM(BlueprintType)
enum class EWeaponState : uint8
{
	EWS_Initial UMETA(DisplayName = "Initial State"),           ///< 初始状态
	EWS_Equipped UMETA(DisplayName = "Equipped"),               ///< 主武器装备状态
	EWS_EquippedSecondary UMETA(DisplayName = "Equipped Secondary"), ///< 副武器装备状态
	EWS_Dropped UMETA(DisplayName = "Dropped"),                 ///< 掉落状态
	EWS_MAX UMETA(DisplayName = "DefaultMAX")                   ///< 默认最大值
};


// enum class EWeaponType;

UCLASS()
class BROKENEDGE_API AWeapon : public AActor
{
	GENERATED_BODY()

public:	
	// Sets default values for this actor's properties
	AWeapon();

	// Called every frame
	virtual void Tick(float DeltaTime) override;

	/// 获取需要网络复制的属性列表
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

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

	
protected:
	UFUNCTION()
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	UPROPERTY()
	class ABrokenEdgeCharacter* BrokenEdgeOwnerCharacter = nullptr;
	
	/** 缓存的武器所有者玩家控制器。 */
	UPROPERTY()
	class ABrokenEdgePlayerController* BrokenEdgeOwnerController;

	
	/// 是否使用服务端回退 @TODO
	// UPROPERTY(Replicated, EditAnywhere)
	// bool bUseServerSideRewind = false;

	
public:	

	void EnableCustomDepth(bool bEnable);
	UFUNCTION()
	void OnSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	                     int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnSphereEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, 
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);


	void ShowPickupWidget(bool bShowWidget);

	/**
 * @brief 武器掉落时调用
 */
	virtual void Dropped();
	
	
	/** 获取武器骨骼网格体组件。 */
	FORCEINLINE USkeletalMeshComponent* GetWeaponMesh() const { return WeaponMesh; }
	
private:
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
	class UAnimationAsset* FireAnimation;

	/** 开火后生成的弹壳 Actor 类型。 */   //@TODO
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
};
