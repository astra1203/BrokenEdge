// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"
#include "BrokenEdge/BrokenEdgeTypes/CombatState.h"
#include "BrokenEdge/BrokenEdgeTypes/TurningInPlace.h"
#include "GameFramework/Character.h"
#include "BrokenEdgeCharacter.generated.h"

class UCameraComponent;
class UCombatComponent;
class AWeapon;
class UBoxComponent;
enum class ETurningInPlace : uint8;
class UInputAction;
class UInputMappingContext;
enum class EVisibilityBasedAnimTickOption : uint8;
enum class EFirstPersonPrimitiveType : uint8;

/** Camera-space weapon fit; source poses stay on a continuous, stationary body. */
USTRUCT(BlueprintType)
struct FStingerViewWeaponFit
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bEnabled = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform Hip = FTransform(FRotator(0, -90, 0), FVector(45, 12, -20));
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform Aim = FTransform(FRotator(0, -90, 0), FVector(45, 0, -8));
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform SourceRightHand;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform SourceLeftHand;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform SourceLeftMagazineOut;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform SourceLeftMagazineIn;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform MagazineGrip;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform MagazineReference;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName MagazineBone = TEXT("b_gun_mag");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1"))
	float MagazineOut = .12f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1"))
	float MagazineIn = .52f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.01", ClampMax="0.99"))
	float SwapAttachFraction = .55f;
};

UCLASS()
class BROKENEDGE_API ABrokenEdgeCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	ABrokenEdgeCharacter();

	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Called every frame
	virtual void Tick(float DeltaTime) override;
	void RotateInPlace(float DeltaTime);
	float CalculateSpeed();
	void AimOffset(float DeltaTime);
	void CalculateAO_Pitch();
	void TurnInPlace(float DeltaTime);

	virtual void NotifyControllerChanged() override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	/// 组件初始化后调用
	virtual void PostInitializeComponents() override;

	/**
 * @brief 播放武器切换动画
	 */
	void PlaySwapMontage();

	void PlayHitReactMontage();

	/**
 * @brief 播放换弹动画
	 */
	void PlayReloadMontage();

	/** 获取换弹动画 */
	FORCEINLINE UAnimMontage* GetReloadMontage() const { return ReloadMontage; }

	void PlayFireMontage(bool bAiming);
	void PlayDefeatMontage();

	/// 是否完成武器切换
	bool bFinishedSwapping = false;

	/** 更新HUD生命值显示 */
	void UpdateHUDHealth();
	void UpdateHUDShield();
	void UpdateHUDAmmo();

	/** 命中碰撞盒映射 */ // unordered_map
	UPROPERTY()
	TMap<FName, UBoxComponent*> HitCollisionBoxes;

	/** 获取战斗组件 */
	FORCEINLINE TObjectPtr<UCombatComponent> GetCombat() const { return Combat; }

	FORCEINLINE TObjectPtr<class UBuffComponent> GetBuff() const { return Buff; }

	FORCEINLINE TObjectPtr<class ULagCompensationComponent> GetLagCompensation() const { return LagCompensation; }

protected:
	/** 头部碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* head = nullptr;

	/** 骨盆碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* pelvis = nullptr;

	/** 第二节脊椎碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* spine_02 = nullptr;

	/** 第三节脊椎碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* spine_03 = nullptr;

	/** 左上臂碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* upperarm_l = nullptr;

	/** 右上臂碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* upperarm_r = nullptr;

	/** 左前臂碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* lowerarm_l = nullptr;

	/** 右前臂碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* lowerarm_r = nullptr;

	/** 左手碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* hand_l = nullptr;

	/** 右手碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* hand_r = nullptr;

	/** 背包碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* backpack = nullptr;

	/** 毯子碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* blanket = nullptr;

	/** 左大腿碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* thigh_l = nullptr;

	/** 右大腿碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* thigh_r = nullptr;

	/** 左小腿碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* calf_l = nullptr;

	/** 右小腿碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* calf_r = nullptr;

	/** 左脚碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* foot_l = nullptr;

	/** 右脚碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* foot_r = nullptr;

public:
	virtual void Jump() override;
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void CrouchButtonPressed();
	void EquipButtonPressed();

	/**
 * @brief 瞄准按钮按下
 */
	void AimButtonPressed();

	/**
	 * @brief 瞄准按钮松开
	 */
	void AimButtonReleased();
	void ReloadButtonPressed();
	void DropButtonPressed();
	void FireButtonPressed();
	void FireButtonReleased();

	/** 获取瞄准偏移偏航值 */
	FORCEINLINE float GetAO_Yaw() const { return AO_Yaw; }

	/** 获取瞄准偏移俯仰值 */
	FORCEINLINE float GetAO_Pitch() const { return AO_Pitch; }

	/** 获取跟随相机 */
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	UFUNCTION(BlueprintPure, Category = Camera)
	bool IsFirstPersonView() const { return bFirstPersonView; }

	// 蜂医等角色可启用专用第一人称表现；旧角色默认使用原有全身视角。
	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person")
	TObjectPtr<USkeletalMesh> FirstPersonMeshAsset = nullptr;
	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person")
	TSubclassOf<UAnimInstance> FirstPersonAnimClass;
	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person")
	bool bStartInFirstPerson = false;
	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person")
	FTransform FirstPersonMeshTransform = FTransform(FRotator(0, -90, 0), FVector(-10, 0, -165));
	UPROPERTY(EditDefaultsOnly, Category = "FirstPerson")
	FVector FirstPersonAimMeshOffset = FVector(20.f, 0.f, 14.f);
	UPROPERTY(EditDefaultsOnly, Category = "FirstPerson")
	TObjectPtr<USkeletalMesh> FirstPersonWeaponSourceMesh;
	UPROPERTY(EditDefaultsOnly, Category = "FirstPerson")
	TObjectPtr<USkeletalMesh> FirstPersonWeaponVisualMesh;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|First Person")
	FStingerViewWeaponFit FirstPersonWeaponFit;
	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person")
	FTransform FirstPersonWeaponTransform = FTransform::Identity;
	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person")
	TObjectPtr<UAnimMontage> FirstPersonFireMontage = nullptr;
	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person")
	TObjectPtr<UAnimMontage> FirstPersonReloadMontage = nullptr;
	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person")
	TObjectPtr<UAnimMontage> FirstPersonSwapMontage = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	FName RightHandBone = TEXT("hand_r");
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Sprint")
	bool bEnableSprint = false;
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Sprint")
	float SprintSpeed = 850.f;
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Sprint")
	TObjectPtr<UInputAction> SprintAction = nullptr;
	UFUNCTION(BlueprintPure, Category = "Movement")
	bool IsSprinting() const { return bSprinting; }

	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person")
	float FirstPersonAimFOV = 65.f;
	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person")
	float FirstPersonAimInterpSpeed = 20.f;
	USkeletalMeshComponent* GetFirstPersonMesh() const { return DedicatedFirstPersonMesh; }
	USkeletalMeshComponent* GetFirstPersonWeaponMesh() const { return FirstPersonWeaponMesh; }
	bool UsesFirstPersonWeaponFit() const;
	FTransform GetFirstPersonGripTarget(FName Socket) const;
	float GetFirstPersonReloadProgress() const;

private:
	void UpdateFirstPersonWeapon();
	void PlayFirstPersonMontage(UAnimMontage* Montage, UAnimMontage* ThirdPersonMontage, FName Section = NAME_None);
	void SyncFirstPersonAction();
	void UpdateFirstPersonWeaponPose();
	void SprintPressed();
	void SprintReleased();
	void UpdateSprint();
	UFUNCTION(Server, Reliable)
	void ServerSetSprintRequested(bool bRequested);
	bool bSprintRequested = false;
	UPROPERTY(Replicated)
	bool bSprinting = false;
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> DedicatedFirstPersonMesh = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> FirstPersonWeaponMesh = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<AWeapon> FirstPersonSourceWeapon = nullptr;
	bool bOriginalWeaponOwnerNoSee = false;
	EFirstPersonPrimitiveType OriginalWeaponPrimitiveType;
	FTransform ViewWeaponTarget = FTransform::Identity;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ViewActionMontage;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ViewActionThirdPersonMontage;
	float ViewActionSectionStart = 0.f;
	float ViewActionSectionLength = 1.f;
	float ViewActionAttachTime = -1.f;
	FDelegateHandle ViewWeaponPoseDelegateHandle;
	void ToggleCameraView();
	void SetFirstPersonView(bool bEnabled);
	void UpdateFirstPersonCameraLocation();
	bool CreateFirstPersonBody();
	void UpdateFirstPersonBodyPose();

	// Local view preference; other players keep seeing the third-person character.
	bool bFirstPersonView = false;
	FTransform ThirdPersonCameraTransform = FTransform::Identity;
	bool bThirdPersonCameraUsesControlRotation = false;
	bool bOriginalMeshOwnerNoSee = false;
	EFirstPersonPrimitiveType OriginalBodyPrimitiveType;
	bool bOriginalMeshCastHiddenShadow = false;
	FDelegateHandle FirstPersonPoseDelegateHandle;

	// Reuses the existing body asset and final pose, with no gameplay collision.
	UPROPERTY(Transient)
	TObjectPtr<class UPoseableMeshComponent> FirstPersonBody = nullptr;
	EVisibilityBasedAnimTickOption OriginalMeshAnimTickOption{};

	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person")
	FName FirstPersonHeadBone = TEXT("head");

	// Offset from BaseEyeHeight, aligned with view yaw (centimeters).
	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person")
	FVector FirstPersonCameraOffset = FVector(10.f, 0.f, 0.f);

	/** MappingContext */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputMappingContext* DefaultMappingContext;

	/** Camera view toggle Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* ToggleCameraViewAction = nullptr;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* LookAction;

	/** Crouch Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* CrouchAction;

	/** Equip Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* EquipAction;

	/** Aim Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* AimAction;

	/** Reload Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* ReloadAction;

	/** Drop Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* DropAction;

	/** Fire Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* FireAction;


	/// 摄像机弹簧臂组件
	UPROPERTY(VisibleAnywhere, Category = Camera)
	class USpringArmComponent* CameraBoom;

	/// 跟随摄像机组件
	UPROPERTY(VisibleAnywhere, Category = Camera)
	class UCameraComponent* FollowCamera;

	/** 头顶UI组件 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class UWidgetComponent* OverheadWidget;

	/// 重叠的武器（带复制通知）
	UPROPERTY(ReplicatedUsing = OnRep_OverlappingWeapon)
	AWeapon* OverlappingWeapon;

	/** 游戏模式 */
	UPROPERTY()
	class ABrokenEdgeGameMode* BrokenEdgeGameMode = nullptr;

	/**
 * @brief 重叠武器复制回调
 * @param LastWeapon 上一个重叠武器
 */
	UFUNCTION()
	void OnRep_OverlappingWeapon(AWeapon* LastWeapon);

	/** 武器切换动画 */
	UPROPERTY(EditAnywhere, Category = Combat)
	UAnimMontage* SwapMontage = nullptr;

	/** 受击反应动画 */
	UPROPERTY(EditAnywhere, Category = Combat)
	UAnimMontage* HitReactMontage = nullptr;

	/** 换弹动画 */
	UPROPERTY(EditAnywhere, Category = Combat)
	UAnimMontage* ReloadMontage = nullptr;

	/** 开火动画 */
	UPROPERTY(EditAnywhere, Category = Combat)
	UAnimMontage* FireWeaponMontage = nullptr;

	/** 被击败动画 */
	UPROPERTY(EditAnywhere, Category = Combat)
	UAnimMontage* DefeatedMontage = nullptr;

	/// 服务器处理装备按钮按下
	UFUNCTION(Server, Reliable)
	void ServerEquipButtonPressed();

	/** 战斗组件 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class UCombatComponent* Combat;

	/** Buff组件 */
	UPROPERTY(VisibleAnywhere)
	class UBuffComponent* Buff;

	/** 延迟补偿组件 */
	UPROPERTY(VisibleAnywhere)
	class ULagCompensationComponent* LagCompensation;

	/** 是否旋转根骨骼 */
	bool bRotateRootBone = false;

	/** 是否被淘汰 */
	// bool bElimmed = false;

	/// 原地转向状态  //@TODO
	ETurningInPlace TurningInPlace = ETurningInPlace::ETIP_NotTurning;

	/** 距离上次移动同步的时间 */
	float TimeSinceLastMovementReplication = 0.f;

	/** 上一帧模拟端旋转 */
	FRotator ProxyRotationLastFrame = FRotator::ZeroRotator;

	/** 当前模拟端旋转 */
	FRotator ProxyRotation = FRotator::ZeroRotator;

	/** 模拟端偏航值 */
	float ProxyYaw = 0.f;

	/** 转身阈值 */
	float TurnThreshold = 0.5f;

	/** 瞄准偏移偏航值 */
	float AO_Yaw = 0.f;

	/** 插值后的瞄准偏移偏航值 */
	float InterpAO_Yaw = 0.f;

	/** 瞄准偏移俯仰值 */
	float AO_Pitch = 0.f;

	/** 起始瞄准旋转 */
	FRotator StartingAimRotation = FRotator::ZeroRotator;

	/** 最大生命值 */
	UPROPERTY(EditAnywhere, Category = "Player States")
	float MaxHealth = 100.f;


	/** 当前生命值 */
	UPROPERTY(ReplicatedUsing = OnRep_Health, EditAnywhere, Category = "Player States")
	float Health = 100.f;

	/**
     * @brief 护盾
    */
	UPROPERTY(EditAnywhere, Category = "Player Stats")
	float MaxShield = 100.f; ///< 最大护盾值

	UPROPERTY(ReplicatedUsing = OnRep_Shield, EditAnywhere, Category = "Player Stats")
	float Shield = 0.f; ///< 当前护盾值

	/**
	 * @brief 护盾复制回调
	 * @param LastShield 上一次护盾值
	 */
	UFUNCTION()
	void OnRep_Shield(float LastShield);

	/**
     * @brief 生命值同步回调
     * @param LastHealth 上一个生命值
    */
	UFUNCTION()
	void OnRep_Health(float LastHealth);

	/** 玩家控制器 */
	UPROPERTY()
	class ABrokenEdgePlayerController* BrokenEdgePlayerController = nullptr;

	/** 是否被击败 */
	bool bDefeated = false;

	/** 淘汰计时器 */
	FTimerHandle RespawnTimer;

	/** 淘汰延迟时间 */
	UPROPERTY(EditDefaultsOnly)
	float RespawnDelay = 3.f;

protected:
	/// 轮询初始化（等待必要组件加载）
	void PollInit();

	UFUNCTION()
	void ReceiveDamage(AActor* DamagedActor, float Damage, const class UDamageType* DamageType,
	                   class AController* InstigatorController, AActor* DamageCauser);

public:
	/**
 * @brief 设置重叠武器
 * @param Weapon 重叠的武器
 */
	void SetOverlappingWeapon(AWeapon* Weapon);

	/** 获取原地转身状态 */
	FORCEINLINE ETurningInPlace GetTurningInPlace() const { return TurningInPlace; }


	FORCEINLINE [[nodiscard]] float GetHealth() const { return Health; }
	FORCEINLINE [[nodiscard]] float GetMaxHealth() const { return MaxHealth; }

	/** 设置生命值 */
	FORCEINLINE void SetHealth(float Amount) { Health = FMath::Clamp(Amount, 0.f, MaxHealth); }

	/// 获取当前护盾值
	FORCEINLINE [[nodiscard]] float GetShield() const { return Shield; }

	/// 设置护盾值
	FORCEINLINE void SetShield(float Amount) { Shield = FMath::Clamp(Amount, 0.f, MaxShield); }

	/// 获取最大护盾值
	FORCEINLINE [[nodiscard]] float GetMaxShield() const { return MaxShield; }

	/** 检查查是否装备了武器 */
	bool IsWeaponEquipped();

	//是否处于瞄准状态
	bool IsAiming();
	AWeapon* GetEquippedWeapon();
	FVector GetHitTarget() const;
	ECombatState GetCombatState() const;
	bool IsLocallyReloading();

	/**
 * @brief 显示狙击镜UI
 * @param bShowScope 是否显示
 */
	UFUNCTION(BlueprintImplementableEvent)
	void ShowSniperScopeWidget(bool bShowScope);


	/** 是否应该旋转根骨骼 */ //inline
	FORCEINLINE bool ShouldRotateRootBone() const { return bRotateRootBone; }

	/** 是否被淘汰 */
	FORCEINLINE bool IsDefeated() const { return bDefeated; }

	/** 是否禁用游戏逻辑 */
	UPROPERTY(Replicated)
	bool bDisableGameplay = false;

	/** 获取是否禁用游戏逻辑 */
	FORCEINLINE bool GetDisableGameplay() const { return bDisableGameplay; }


	AWeapon* GetOverlappingWeapon() { return OverlappingWeapon; }

	/**
 * @brief 同步移动回调
 */
	virtual void OnRep_ReplicatedMovement() override;

	/**
 * @brief 模拟端转身
 */
	void SimProxiesTurn();

	/**
 * @brief 角色淘汰
	 * @param bPlayerLeftGame 玩家是否离开游戏
	 */
	void Defeated(bool bPlayerLeftGame);

	/**
 * @brief 多播淘汰事件
 * @param bPlayerLeftGame 玩家是否离开游戏
 */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastDefeated(bool bPlayerLeftGame);

	/** 是否离开游戏 */
	bool bLeftGame = false;

	void RespawnTimerFinished();
	/**
 * @brief 丢弃或销毁所有武器
	 */
	void DropWeapons();
};
