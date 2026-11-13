// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"
#include "BrokenEdge/BrokenEdgeTypes/CombatState.h"
#include "BrokenEdge/BrokenEdgeTypes/TurningInPlace.h"
#include "GameFramework/Character.h"
#include "BrokenEdgeCharacter.generated.h"

class UCameraComponent;
class UAnimInstance;
class UCombatComponent;
class AWeapon;
enum class ETurningInPlace : uint8;
class UInputAction;
class UInputMappingContext;
enum class EVisibilityBasedAnimTickOption : uint8;
enum class EFirstPersonPrimitiveType : uint8;

enum class EExtractionVitalsReason : uint8 { Healing, Damage, Bleeding, Restore };
struct FExtractionVitalsDelta
{
    float Health = 0, Shield = 0;
    EExtractionVitalsReason Reason = EExtractionVitalsReason::Healing;
    AController* Instigator = nullptr;
    AActor* Causer = nullptr;
};
struct FExtractionVitalsChange
{
    float Health = 0, Shield = 0, DownedHealth = 0;
    bool Applied = false, Downed = false, Defeated = false;
};
DECLARE_MULTICAST_DELEGATE_OneParam(FExtractionVitalsChanged, const FExtractionVitalsChange&);

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
	class UExtractionInteractionComponent* GetExtractionInteraction() const { return ExtractionInteraction; }
	friend class UExtractionHealth;
	class UExtractionHealth* GetExtractionHealth() const { return ExtractionHealth; }
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<class UExtractionHealth> ExtractionHealth;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UExtractionInteractionComponent> ExtractionInteraction;
	// Sets default values for this character's properties
	ABrokenEdgeCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<class UExtractionTraversalComponent> Traversal;
	class UExtractionTraversalComponent* GetTraversal() const { return Traversal; }
	bool IsTraversing() const;

	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Called every frame
	virtual void Tick(float DeltaTime) override;
	/** 守卫统一使用 AI 控制器的期望朝向，避免玩家持枪/原地转身逻辑覆盖导航和 SetFocus。 */
	void ConfigureGuardRotation();
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
	UAnimMontage* GetReloadMontage() const;
	UAnimMontage* GetSwapMontage() const;
	UAnimMontage* GetViewReloadMontage() const;
	UAnimMontage* GetViewSwapMontage() const;
	const FStingerViewWeaponFit& GetViewWeaponFit() const;

	void PlayFireMontage(bool bAiming);
	void PlayDefeatMontage();

	/// 是否完成武器切换
	bool bFinishedSwapping = false;

	/** 更新HUD生命值显示 */
	void UpdateHUDHealth();
	void UpdateHUDShield();
	void UpdateHUDAmmo();

	/** 获取战斗组件 */
	FORCEINLINE TObjectPtr<UCombatComponent> GetCombat() const { return Combat; }
	class UExtractionCombatComponent* GetExtractionCombat() const { return ExtractionCombat; }
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UExtractionCombatComponent> ExtractionCombat;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<class UExtractionMeleeComponent> Melee;

	FORCEINLINE TObjectPtr<class UBuffComponent> GetBuff() const { return Buff; }

	FORCEINLINE TObjectPtr<class ULagCompensationComponent> GetLagCompensation() const { return LagCompensation; }

public:
	virtual void Jump() override;
	virtual void StopJumping() override;
	// 游泳时按住空格上浮（本地输入，经移动组件加速度同步）。
	bool bSwimUp = false;
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void CrouchButtonPressed();
	void EquipButtonPressed();
	/** F 键：撤离模式里对着钥匙房门按 F 开门（需要对应钥匙卡）。 */
	void InteractDoorPressed();

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
	// Cancellation must work even after gameplay is disabled or the pawn is defeated.
	void StopHeldCombatInput();

	/** 获取瞄准偏移偏航值 */
	FORCEINLINE float GetAO_Yaw() const { return AO_Yaw; }

	/** 获取瞄准偏移俯仰值 */
	FORCEINLINE float GetAO_Pitch() const { return AO_Pitch; }

	/** 获取跟随相机 */
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	UFUNCTION(BlueprintPure, Category = Camera)
	bool IsFirstPersonView() const { return bFirstPersonView; }

	// 蜂医等角色使用专用第一人称表现；未配置时使用全身网格副本呈现第一人称。
	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person")
	TObjectPtr<USkeletalMesh> FirstPersonMeshAsset = nullptr;
	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person")
	TSubclassOf<UAnimInstance> FirstPersonAnimClass;
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
	// 技能表现只影响动画，不参与救援、治疗、烟雾的服务器结算。
	UPROPERTY(EditDefaultsOnly, Category = "Abilities|Animation")
	TObjectPtr<UAnimMontage> HealMontage = nullptr;
	UPROPERTY(EditDefaultsOnly, Category = "Abilities|Animation")
	TObjectPtr<UAnimMontage> FirstPersonHealMontage = nullptr;
	// 激素枪射向队友（官方 MedicalGun Fire）；未配置时回退到自用的 Heal 蒙太奇。
	UPROPERTY(EditDefaultsOnly, Category = "Abilities|Animation")
	TObjectPtr<UAnimMontage> HealOtherMontage = nullptr;
	UPROPERTY(EditDefaultsOnly, Category = "Abilities|Animation")
	TObjectPtr<UAnimMontage> FirstPersonHealOtherMontage = nullptr;
	UPROPERTY(EditDefaultsOnly, Category = "Abilities|Animation")
	TObjectPtr<UAnimMontage> ReviveMontage = nullptr;
	UPROPERTY(EditDefaultsOnly, Category = "Abilities|Animation")
	TObjectPtr<UAnimMontage> FirstPersonReviveMontage = nullptr;
	UPROPERTY(EditDefaultsOnly, Category = "Abilities|Animation")
	TObjectPtr<UAnimMontage> SmokeMontage = nullptr;
	UPROPERTY(EditDefaultsOnly, Category = "Abilities|Animation")
	TObjectPtr<UAnimMontage> FirstPersonSmokeMontage = nullptr;
	/**
	 * 播放技能表现蒙太奇；ActionKind 与 EExtractionAction 取值一致（1 Revive / 2 Heal / 3 Smoke），
	 * 另加表现专用的 4 = 激素枪打队友。
	 * Seconds 是服务器给定的动作时长，用于把蒙太奇拉伸到与结算同步。
	 */
	void PlayAbilityMontage(uint8 ActionKind, float Seconds);
	/** 停止技能表现并复位蒙太奇状态；重复调用安全。 */
	void StopAbilityMontage();
	bool IsAbilityMontageActive() const { return AbilityMontage != nullptr; }
	/**
	 * 只在 Development 构建里生效的「卡住」探针：持续有移动输入、贴地却几乎不位移时，
	 * 打一条含移动模式/地面/卡住对象的日志，用于定位位置相关的走不动问题。
	 */
	void DiagnoseStuckMovement(float DeltaTime);

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

	void UpdateSprint();

	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person")
	float FirstPersonAimFOV = 65.f;
	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person")
	float FirstPersonAimInterpSpeed = 20.f;
	USkeletalMeshComponent* GetFirstPersonMesh() const { return DedicatedFirstPersonMesh; }
	// 爬梯时第一人称改看自己的完整身体（隐藏头部），而不是只有两条胳膊的手臂网格；仅本地第一人称生效。
	void SetLadderBodyView(bool bOn);
	bool bLadderBodyView = false;
	FName LadderHiddenBone;
	// 退出爬梯后相机还平滑回眼高的剩余秒数。
	float LadderCameraBlend = 0.f;
	USkeletalMeshComponent* GetFirstPersonWeaponMesh() const { return FirstPersonWeaponMesh; }
	bool UsesFirstPersonWeaponFit() const;
	bool IsFirstPersonScopeActive() const;
	FTransform GetFirstPersonGripTarget(FName Socket) const;
	float GetFirstPersonReloadProgress() const;
	void RefreshFirstPersonWorldWeapons();
	void TrackFirstPersonWorldWeapon(AWeapon* Weapon);

private:
	void HandleVitalsChanged(const FExtractionVitalsChange& Change);
	void UpdateFirstPersonWeapon();
	void PlayFirstPersonMontage(UAnimMontage* Montage, UAnimMontage* ThirdPersonMontage, FName Section = NAME_None);
	void SyncFirstPersonAction();
	// 第一人称切枪：先把手里的枪放下，等换枪落地后再播新枪的拔枪动作；返回放下程度 0~1。
	float SyncFirstPersonSwap();
	float ViewSwapStart = -1.f, ViewSwapUnequip = .45f, ViewSwapEquip = .4f, ViewSwapDrawStart = -1.f;
	TWeakObjectPtr<AWeapon> ViewSwapFrom;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ViewSwapDraw;
	FVector ViewSwapOffsetApplied = FVector::ZeroVector;
	void UpdateFirstPersonWeaponPose();
	void SprintPressed();
	void SprintReleased();
	UFUNCTION(Server, Reliable)
	void ServerSetSprintRequested(bool bRequested);
	bool bSprintRequested = false;
	UPROPERTY(Replicated)
	bool bSprinting = false;
	/** 搜打撤体力：服务器与本地各自按同一规则推进，服务器值只同步给本人用于校正。 */
	UPROPERTY(Replicated)
	float Stamina = 100;
	double StaminaRegenAt = 0;
	/** 服务器：下次向守卫广播冲刺脚步声的时刻。 */
	double NextFootstepNoiseAt = 0;
	// 按本帧是否冲刺推进体力；返回当前是否还允许冲刺。
	bool UpdateStamina(bool bWantsSprint, float DeltaSeconds);

public:
	float GetStamina() const { return Stamina; }
	float GetStaminaFraction() const;
	/** 搜打撤趴下（Z 切换）：本地立即生效并请求服务器，服务器值复制给其他端。 */
	void ToggleProne();
	bool IsProne() const { return bProne; }
	/**
	 * 滑索：服务器开始/结束并复制给所有端。骑乘时切到飞行模式，本地操控端每帧沿 ZipFrom→ZipTo
	 * 加移动输入（服务器按同样输入模拟），到端点或按空格松手后落下（在水里则直接游泳）。
	 */
	void StartZipline(class AExtractionZipline* Line, const FVector& From, const FVector& To);
	void EndZipline();
	bool IsOnZipline() const { return Zipline != nullptr; }
	UFUNCTION(Server, Reliable)
	void ServerEndZipline();

private:
	UPROPERTY(ReplicatedUsing=OnRep_Zipline)
	TObjectPtr<class AExtractionZipline> Zipline;
	UPROPERTY(Replicated)
	FVector ZipFrom = FVector::ZeroVector;
	UPROPERTY(Replicated)
	FVector ZipTo = FVector::ZeroVector;
	UFUNCTION()
	void OnRep_Zipline();
	// 进出飞行模式与滑行速度，只在服务器和本地操控端生效；重复调用无副作用。
	void ApplyZipline(bool bOn);
	void TickZipline();
	bool bZipApplied = false;
	float ZipSavedFlySpeed = 0;
	double ZipStartedAt = 0;
	UPROPERTY(Replicated)
	bool bProne = false;
	UFUNCTION(Server, Reliable)
	void ServerSetProne(bool bNewProne);
	// 进入/退出趴下时切换蹲姿与移速、视线高度，并保存原值以便恢复。
	void ApplyProne(bool bNewProne);
	bool bProneApplied = false;
	float ProneSavedCrouchSpeed = 0, ProneSavedEyeHeight = 0;
	/**
	 * 第三人称趴姿动画（AnimStarterPack 重定向到 Stinger，retarget_prone.py 生成）：
	 * 趴下先播 StandToProne，再循环 ProneIdle；起身播 ProneToStand；趴着开火 / 换弹改用趴姿版本。
	 */
	UPROPERTY(EditDefaultsOnly, Category="Extraction|Prone")
	TSoftObjectPtr<class UAnimSequence> StandToProneAnim;
	UPROPERTY(EditDefaultsOnly, Category="Extraction|Prone")
	TSoftObjectPtr<class UAnimSequence> ProneIdleAnim;
	UPROPERTY(EditDefaultsOnly, Category="Extraction|Prone")
	TSoftObjectPtr<class UAnimSequence> ProneToStandAnim;
	UPROPERTY(EditDefaultsOnly, Category="Extraction|Prone")
	TSoftObjectPtr<class UAnimSequence> ProneFireAnim;
	UPROPERTY(EditDefaultsOnly, Category="Extraction|Prone")
	TSoftObjectPtr<class UAnimSequence> ProneReloadAnim;
	// 各端按复制的 bProne 播放趴姿动画（含模拟代理），被其他蒙太奇打断后自动接回趴姿待机。
	void UpdateProneAnimation();
	// 当前处于趴姿表现时返回趴姿序列（未加载则同步加载），否则返回空。
	class UAnimSequence* ProneAnim(const TSoftObjectPtr<class UAnimSequence>& Anim) const;
	bool bProneAnimating = false;
	double ProneIdleAt = 0;
	UPROPERTY(Transient)
	TObjectPtr<class UAnimMontage> ProneMontage;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> DedicatedFirstPersonMesh = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> FirstPersonWeaponMesh = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<AWeapon> FirstPersonSourceWeapon = nullptr;
	TSet<TWeakObjectPtr<AWeapon>> FirstPersonWorldWeapons;
	FTransform ViewWeaponTarget = FTransform::Identity;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ViewActionMontage;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ViewActionThirdPersonMontage;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> AbilityMontage;
	/** DiagnoseStuckMovement 的累积时长与单次上报标记。 */
	float StuckSeconds = 0.f;
	bool bStuckReported = false;
	/** 本次会话累计卡住次数，便于测试与现场确认探针是否命中。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Diagnostics", meta = (AllowPrivateAccess = "true"))
	int32 StuckEpisodes = 0;
	float ViewActionSectionStart = 0.f;
	float ViewActionSectionLength = 1.f;
	float ViewActionAttachTime = -1.f;
	FDelegateHandle ViewWeaponPoseDelegateHandle;
	void InitializeFirstPersonView();
	void ReleaseFirstPersonView();
	void UpdateFirstPersonCameraLocation();
	bool CreateFirstPersonBody();
	void UpdateFirstPersonBodyPose();

	// 本地第一人称表现是否已初始化；不再作为可切换的视角偏好。
	bool bFirstPersonView = false;
	bool bFirstPersonInitializationFailed = false;
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

	// Hide accessory sections on the local view mesh without changing the world body.
	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person")
	TArray<FName> FirstPersonHiddenMaterialSlots;

	// Offset from BaseEyeHeight, aligned with view yaw (centimeters).
	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person")
	FVector FirstPersonCameraOffset = FVector(10.f, 0.f, 0.f);

	/** MappingContext */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputMappingContext* DefaultMappingContext;


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
	// 搜打撤模式：把这次伤害的结果（命中/击倒/击杀）发给攻击者，把伤害来源方向发给受击者。
	void NotifyCombatFeedback(AController* Attacker, AActor* Causer, bool bWasDowned, float Damage);

public:
	/**
 * @brief 设置重叠武器
 * @param Weapon 重叠的武器
 */
	void SetOverlappingWeapon(AWeapon* Weapon);

	/** 获取原地转身状态 */
	FORCEINLINE ETurningInPlace GetTurningInPlace() const { return TurningInPlace; }


	FORCEINLINE [[nodiscard]] float GetHealth() const { return Health; }
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UExtractionAbilities> ExtractionAbilities;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UExtractionInventoryComponent> GuardInventory;
	UPROPERTY(Replicated)
	bool bExtractionGuard = false;
	/** 守卫警觉度：0 巡逻、1 搜索中、2 交战中；服务器写入，各端头顶标签据此显示。 */
	UPROPERTY(Replicated)
	uint8 ExtractionAlert = 0;
	/**
	 * 守卫身体模型相对胶囊的偏航修正。本项目 Stinger 模型使用 -90 度，使模型正面对应 Actor +X。
	 * 替换为其他朝向的模型时，在敌人蓝图类默认值中调整；不影响玩家角色的模型配置。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="AI|Presentation")
	float GuardMeshYawOffset = -90.f;
	UPROPERTY(Replicated)
	int32 ExtractionGuardId = INDEX_NONE;
	bool bWasExtractionDowned = false;
	float ExtractionSavedCrouchSpeed = 300;
	float ExtractionFractureCrouchSpeed = 300;
	bool bExtractionFractureSpeedApplied = false;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UTextRenderComponent> ExtractionLabel;
	void UpdateExtractionLabel();
	UExtractionAbilities* GetExtractionAbilities() const { return ExtractionAbilities; }
	FORCEINLINE [[nodiscard]] float GetMaxHealth() const { return MaxHealth; }

	/** 唯一服务器生命/护盾入口；返回钳制后的实际变化，非空变化只广播一次。 */
	FExtractionVitalsChange ApplyVitalsChange(const FExtractionVitalsDelta& Delta);
    FExtractionVitalsChanged OnVitalsChanged;
    // Resolve team/mode damage exactly once before armor; application never recalculates it.
    float CalculateIncomingDamage(float Damage, AController* Attacker);
    FExtractionVitalsChange ApplyResolvedDamage(float Damage, AController* Attacker, AActor* Causer);
#if WITH_DEV_AUTOMATION_TESTS
    int32 TestDamageCalculations = 0;
#endif

	/// 获取当前护盾值
	FORCEINLINE [[nodiscard]] float GetShield() const { return Shield; }

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
