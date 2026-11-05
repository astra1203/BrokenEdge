// Fill out your copyright notice in the Description page of Project Settings.


#include "BrokenEdgeCharacter.h"
#include "Misc/ScopeExit.h"
#include "GameFramework/PlayerState.h"
#include "BrokenEdge/Extraction/ExtractionCombatComponent.h"
#include "BrokenEdge/Extraction/ExtractionInteractionComponent.h"
#include "BrokenEdge/Extraction/ExtractionAbilities.h"
#include "BrokenEdge/Extraction/ExtractionHealth.h"
#include "BrokenEdge/Extraction/ExtractionState.h"
#include "BrokenEdge/Extraction/ExtractionSmoke.h"
#include "Components/TextRenderComponent.h"
#include "BrokenEdge/Extraction/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/ExtractionGameMode.h"
#include "BrokenEdge/Extraction/ExtractionGuard.h"
#include "BrokenEdge/Extraction/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/ExtractionPlayerController.h"
#include "StingerFirstPersonAnimInstance.h"
#include "BrokenEdge/Traversal/TraversalMovementComponent.h"
#include "BrokenEdge/Traversal/ExtractionTraversalComponent.h"
#include "Animation/AnimMontage.h"
#include "BrokenEdge/Weapon/WeaponPresentation.h"
#include "BrokenEdge/Extraction/ExtractionMeleeComponent.h"
#include "BrokenEdge/Extraction/ExtractionFragGrenade.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "BrokenEdge/BrokenEdge.h"
#include "BrokenEdge/BrokenEdgeTypes/TurningInPlace.h"
#include "BrokenEdge/Component/BuffComponent.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Component/LagCompensationComponent.h"
#include "BrokenEdge/GameMode/BrokenEdgeGameMode.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Net/UnrealNetwork.h"
#include "BrokenEdge/Extraction/ExtractionZipline.h"
#include "GameFramework/PhysicsVolume.h"


// Sets default values
ABrokenEdgeCharacter::ABrokenEdgeCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UTraversalMovementComponent>(
		ACharacter::CharacterMovementComponentName))
{
	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	// 趴姿动画默认路径（retarget_prone.py 生成），蓝图可覆盖。
	StandToProneAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Stinger/Animations/Prone/Stand_To_Prone_Stinger.Stand_To_Prone_Stinger")));
	ProneIdleAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Stinger/Animations/Prone/Prone_Idle_Stinger.Prone_Idle_Stinger")));
	ProneToStandAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Stinger/Animations/Prone/Prone_To_Stand_Stinger.Prone_To_Stand_Stinger")));
	ProneFireAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Stinger/Animations/Prone/Prone_Fire_1_Stinger.Prone_Fire_1_Stinger")));
	ProneReloadAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Stinger/Animations/Prone/Prone_Reload_Rifle_Stinger.Prone_Reload_Rifle_Stinger")));

	// 固定第一人称：相机始终挂在胶囊体，位置由角色眼高更新。
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(GetCapsuleComponent());
	FollowCamera->bUsePawnControlRotation = true;
	// 射击游戏关掉动态模糊：转镜头时画面不拖影，也压过地图后处理体积里的设置。
	FollowCamera->PostProcessSettings.bOverride_MotionBlurAmount = true;
	FollowCamera->PostProcessSettings.MotionBlurAmount = 0.f;
	FollowCamera->PostProcessBlendWeight = 1.f;

	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;

	OverheadWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("OverheadWidget"));
	OverheadWidget->SetupAttachment(RootComponent);

	Traversal = CreateDefaultSubobject<UExtractionTraversalComponent>(TEXT("Traversal"));
	Combat = CreateDefaultSubobject<UCombatComponent>(TEXT("CombatComponent"));
	Combat->SetIsReplicated(true);
	ExtractionCombat = CreateDefaultSubobject<UExtractionCombatComponent>(TEXT("ExtractionCombat"));
	Melee = CreateDefaultSubobject<UExtractionMeleeComponent>(TEXT("Melee"));
	ExtractionLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ExtractionLabel"));
	ExtractionLabel->SetupAttachment(GetRootComponent());
	ExtractionLabel->SetRelativeLocation(FVector(0, 0, 115));
	ExtractionLabel->SetHorizontalAlignment(EHTA_Center);
	ExtractionLabel->SetWorldSize(20);
	ExtractionLabel->SetVisibility(false);
	ExtractionLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ExtractionAbilities = CreateDefaultSubobject<UExtractionAbilities>(TEXT("ExtractionAbilities"));
	ExtractionHealth = CreateDefaultSubobject<UExtractionHealth>(TEXT("ExtractionHealth"));
	GuardInventory = CreateDefaultSubobject<UExtractionInventoryComponent>(TEXT("GuardInventory"));
	ExtractionInteraction = CreateDefaultSubobject<UExtractionInteractionComponent>(TEXT("ExtractionInteraction"));

	Buff = CreateDefaultSubobject<UBuffComponent>(TEXT("BuffComponent"));
	Buff->SetIsReplicated(true);

	LagCompensation = CreateDefaultSubobject<ULagCompensationComponent>(TEXT("LagCompensation"));

	GetCharacterMovement()->NavAgentProps.bCanCrouch = true;
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECollisionChannel::ECC_Camera, ECollisionResponse::ECR_Ignore);
	GetMesh()->SetCollisionObjectType(ECC_SkeletalMesh);
	GetMesh()->SetCollisionResponseToChannel(ECollisionChannel::ECC_Camera, ECollisionResponse::ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECollisionChannel::ECC_Visibility, ECollisionResponse::ECR_Block);
	GetCharacterMovement()->RotationRate = FRotator(0.f, 0.f, 850.f);

	TurningInPlace = ETurningInPlace::ETIP_NotTurning;
	// NetUpdateFrequency = 66.f;
	// MinNetUpdateFrequency = 33.f;
	SetNetUpdateFrequency(66.f);
	SetMinNetUpdateFrequency(33.f);

	/*
	* Hit Boxes(用于服务器倒带命中判断)
	*/

	head = CreateDefaultSubobject<UBoxComponent>(TEXT("head"));
	head->SetupAttachment(GetMesh(), FName("head"));
	HitCollisionBoxes.Add(FName("head"), head);

	pelvis = CreateDefaultSubobject<UBoxComponent>(TEXT("pelvis"));
	pelvis->SetupAttachment(GetMesh(), FName("pelvis"));
	HitCollisionBoxes.Add(FName("pelvis"), pelvis);

	spine_02 = CreateDefaultSubobject<UBoxComponent>(TEXT("spine_02"));
	spine_02->SetupAttachment(GetMesh(), FName("spine_02"));
	HitCollisionBoxes.Add(FName("spine_02"), spine_02);

	spine_03 = CreateDefaultSubobject<UBoxComponent>(TEXT("spine_03"));
	spine_03->SetupAttachment(GetMesh(), FName("spine_03"));
	HitCollisionBoxes.Add(FName("spine_03"), spine_03);

	upperarm_l = CreateDefaultSubobject<UBoxComponent>(TEXT("upperarm_l"));
	upperarm_l->SetupAttachment(GetMesh(), FName("upperarm_l"));
	HitCollisionBoxes.Add(FName("upperarm_l"), upperarm_l);

	upperarm_r = CreateDefaultSubobject<UBoxComponent>(TEXT("upperarm_r"));
	upperarm_r->SetupAttachment(GetMesh(), FName("upperarm_r"));
	HitCollisionBoxes.Add(FName("upperarm_r"), upperarm_r);

	lowerarm_l = CreateDefaultSubobject<UBoxComponent>(TEXT("lowerarm_l"));
	lowerarm_l->SetupAttachment(GetMesh(), FName("lowerarm_l"));
	HitCollisionBoxes.Add(FName("lowerarm_l"), lowerarm_l);

	lowerarm_r = CreateDefaultSubobject<UBoxComponent>(TEXT("lowerarm_r"));
	lowerarm_r->SetupAttachment(GetMesh(), FName("lowerarm_r"));
	HitCollisionBoxes.Add(FName("lowerarm_r"), lowerarm_r);

	hand_l = CreateDefaultSubobject<UBoxComponent>(TEXT("hand_l"));
	hand_l->SetupAttachment(GetMesh(), FName("hand_l"));
	HitCollisionBoxes.Add(FName("hand_l"), hand_l);

	hand_r = CreateDefaultSubobject<UBoxComponent>(TEXT("hand_r"));
	hand_r->SetupAttachment(GetMesh(), FName("hand_r"));
	HitCollisionBoxes.Add(FName("hand_r"), hand_r);

	blanket = CreateDefaultSubobject<UBoxComponent>(TEXT("blanket"));
	blanket->SetupAttachment(GetMesh(), FName("backpack"));
	HitCollisionBoxes.Add(FName("blanket"), blanket);

	backpack = CreateDefaultSubobject<UBoxComponent>(TEXT("backpack"));
	backpack->SetupAttachment(GetMesh(), FName("backpack"));
	HitCollisionBoxes.Add(FName("backpack"), backpack);

	thigh_l = CreateDefaultSubobject<UBoxComponent>(TEXT("thigh_l"));
	thigh_l->SetupAttachment(GetMesh(), FName("thigh_l"));
	HitCollisionBoxes.Add(FName("thigh_l"), thigh_l);

	thigh_r = CreateDefaultSubobject<UBoxComponent>(TEXT("thigh_r"));
	thigh_r->SetupAttachment(GetMesh(), FName("thigh_r"));
	HitCollisionBoxes.Add(FName("thigh_r"), thigh_r);

	calf_l = CreateDefaultSubobject<UBoxComponent>(TEXT("calf_l"));
	calf_l->SetupAttachment(GetMesh(), FName("calf_l"));
	HitCollisionBoxes.Add(FName("calf_l"), calf_l);

	calf_r = CreateDefaultSubobject<UBoxComponent>(TEXT("calf_r"));
	calf_r->SetupAttachment(GetMesh(), FName("calf_r"));
	HitCollisionBoxes.Add(FName("calf_r"), calf_r);

	foot_l = CreateDefaultSubobject<UBoxComponent>(TEXT("foot_l"));
	foot_l->SetupAttachment(GetMesh(), FName("foot_l"));
	HitCollisionBoxes.Add(FName("foot_l"), foot_l);

	foot_r = CreateDefaultSubobject<UBoxComponent>(TEXT("foot_r"));
	foot_r->SetupAttachment(GetMesh(), FName("foot_r"));
	HitCollisionBoxes.Add(FName("foot_r"), foot_r);

	// 配置所有命中碰撞盒的碰撞属性
	for (auto& Box : HitCollisionBoxes)
	{
		if (Box.Value)
		{
			Box.Value->SetCollisionObjectType(ECC_HitBox);
			Box.Value->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
			Box.Value->SetCollisionResponseToChannel(ECC_HitBox, ECollisionResponse::ECR_Block);
			Box.Value->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
}

void ABrokenEdgeCharacter::ReceiveDamage(AActor* DamagedActor, float Damage, const class UDamageType* DamageType,
                                         class AController* InstigatorController, AActor* DamageCauser)
{
	BrokenEdgeGameMode = BrokenEdgeGameMode == nullptr
		                     ? GetWorld()->GetAuthGameMode<ABrokenEdgeGameMode>()
		                     : BrokenEdgeGameMode;

	if (bDefeated || BrokenEdgeGameMode == nullptr)return;
	Damage = BrokenEdgeGameMode->CalculateDamage(InstigatorController, GetController(), Damage);
	// 搜打撤模式在函数返回时统一发战斗反馈，覆盖倒地扣血、击倒、击杀各条返回路径。
	bool bFeedback = false;
	const bool bWasDowned = ExtractionAbilities->IsDowned();
	ON_SCOPE_EXIT { if (bFeedback) NotifyCombatFeedback(InstigatorController, DamageCauser, bWasDowned, Damage); };
	if (ExtractionInteraction->IsEnabled())
	{
		if (Damage <= 0) return;
		bFeedback = true;
		ExtractionInteraction->CancelMedical();
		ExtractionAbilities->Damaged();
		if (auto* GM = Cast<AExtractionGameMode>(BrokenEdgeGameMode); GM && GM->DamageDowned(this, Damage)) return;
	}

	float DamageToHealth = Damage;
	if (Shield > 0.f && !ExtractionCombat->IsEnabled())
	{
		if (Shield >= Damage)
		{
			Shield = FMath::Clamp(Shield - Damage, 0.f, MaxShield);
			DamageToHealth = 0.f;
		}
		else
		{
			DamageToHealth = FMath::Clamp(DamageToHealth - Shield, 0.f, Damage);
			Shield = 0.f;
		}
	}

	Health = FMath::Clamp(Health - DamageToHealth, 0.f, MaxHealth);

	UpdateHUDHealth();
	UpdateHUDShield();
	PlayHitReactMontage();

	if (Health <= 0.f)
	{
		if (auto* GM = Cast<AExtractionGameMode>(BrokenEdgeGameMode))
		{
			GM->DefeatCombatant(this, InstigatorController);
			return;
		}
		BrokenEdgePlayerController = BrokenEdgePlayerController == nullptr
			                             ? Cast<ABrokenEdgePlayerController>(Controller)
			                             : BrokenEdgePlayerController;

		ABrokenEdgePlayerController* AttackerController = Cast<ABrokenEdgePlayerController>(InstigatorController);

		BrokenEdgeGameMode->PlayerDefeated(this,
		                                   BrokenEdgePlayerController, AttackerController);
	}
}

void ABrokenEdgeCharacter::NotifyCombatFeedback(AController* Attacker, AActor* Causer, bool bWasDowned, float Damage)
{
	const uint8 Result = bDefeated ? 2 : !bWasDowned && ExtractionAbilities->IsDowned() ? 1 : 0;
	if (auto* Shooter = Cast<AExtractionPlayerController>(Attacker); Shooter && Attacker != GetController())
	{
		const APlayerState* State = GetPlayerState();
		Shooter->ClientCombatHit(Result, bExtractionGuard ? TEXT("守卫") : State ? State->GetPlayerName() : TEXT("敌人"));
	}
	if (auto* Victim = Cast<AExtractionPlayerController>(GetController()); Victim && Attacker != Victim)
	{
		const AActor* Source = Attacker && Attacker->GetPawn() ? Attacker->GetPawn() : Causer;
		if (Source && Source != this) Victim->ClientDamaged(Source->GetActorLocation());
		// 阵亡回顾：攻击者名称、所用武器、距离。
		const auto* Shooter = Attacker ? Cast<ABrokenEdgeCharacter>(Attacker->GetPawn()) : nullptr;
		FString Name = TEXT("未知来源");
		if (Shooter && Shooter->bExtractionGuard) Name = TEXT("守卫");
		else if (const APlayerState* State = Attacker ? Attacker->GetPlayerState<APlayerState>() : nullptr)
			Name = State->GetPlayerName();
		FString Weapon = TEXT("未知");
		if (Causer && Causer->IsA<AExtractionFragGrenade>()) Weapon = TEXT("破片手雷");
		else if (Shooter && Shooter->Melee && Shooter->Melee->Active) Weapon = TEXT("战术刀");
		else if (Shooter)
			if (auto* Gun = const_cast<ABrokenEdgeCharacter*>(Shooter)->GetEquippedWeapon())
				if (const auto* D = UExtractionItemDefinition::Get(Gun->ExtractionDefinition)) Weapon = D->Label.ToString();
		Victim->RecordDamage(Name, Weapon, Source ? FVector::Dist2D(Source->GetActorLocation(), GetActorLocation()) / 100 : 0,
		                     Damage);
	}
}

// Called when the game starts or when spawned
void ABrokenEdgeCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (ExtractionCombat->IsEnabled() && OverheadWidget)
	{
		// A name plate must not intercept the aiming ray or reveal enemies through cover.
		OverheadWidget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		OverheadWidget->SetVisibility(false);
	}

	if (HasAuthority())
	{
		OnTakeAnyDamage.AddDynamic(this, &ABrokenEdgeCharacter::ReceiveDamage);
	}
}

void ABrokenEdgeCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseFirstPersonView();
	RefreshFirstPersonWorldWeapons();
	FirstPersonWorldWeapons.Reset();
	Super::EndPlay(EndPlayReason);
}

// Called every frame
void ABrokenEdgeCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateSprint();
	UpdateProneAnimation();
	TickZipline();
	TickAutoCrouch();
	if (bSwimUp)
	{
		if (GetCharacterMovement()->IsSwimming()) AddMovementInput(FVector::UpVector, 1.f);
		else bSwimUp = false;
	}
	DiagnoseStuckMovement(DeltaTime);
	UpdateExtractionLabel();
	// 控制权和组件可能晚于 BeginPlay 就绪；初始化成功或明确失败后不再重复创建。
	if (!bFirstPersonView && !bFirstPersonInitializationFailed) InitializeFirstPersonView();
	if (bFirstPersonView)
	{
		UpdateFirstPersonCameraLocation();
		UpdateFirstPersonWeapon();
	}
	//@TODO
	RotateInPlace(DeltaTime);
	PollInit();
	// bUseControllerRotationYaw = false;
	// GetCharacterMovement()->bOrientRotationToMovement=true;
	// UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s %s"), 
	// 	GFrameCounter, TEXT(__FUNCTION__), __LINE__, 
	// 	bUseControllerRotationYaw?TEXT("true"):TEXT("false"),
	// 	GetCharacterMovement()->bOrientRotationToMovement?TEXT("true"):TEXT("false"));
}


/**
 * @brief 原地旋转处理
 * @param DeltaTime 帧时间
 */
void ABrokenEdgeCharacter::ConfigureGuardRotation()
{
	if (!bExtractionGuard) return;
	// 控制器：巡逻时由 PathFollowing 的 Move Focus 指向路径，交战时由 Gameplay Focus 指向玩家。
	// 角色：由 CharacterMovement 平滑追随这个期望朝向，不再每帧直接套用玩家镜头旋转。
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	auto* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = false;
	Movement->bUseControllerDesiredRotation = !bDisableGameplay && !bDefeated && !IsTraversing();
	// FRotator 的参数顺序是 Pitch、Yaw、Roll；旧设置 (0, 0, 850) 实际上让 Yaw 转速为零。
	Movement->RotationRate = FRotator(0.f, 360.f, 0.f);
	if (GetMesh()->GetSkeletalMeshAsset() && FMath::IsFinite(GuardMeshYawOffset))
	{
		// BP_EnemyCharacter 当前使用 Stinger 网格但相对 Yaw 为 0，视觉正面会与导航正面错开 90 度。
		// 只修正守卫网格的 Yaw，保留蓝图中的位置、缩放以及 Pitch/Roll。
		FRotator MeshRotation = GetMesh()->GetRelativeRotation();
		if (!FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(MeshRotation.Yaw, GuardMeshYawOffset), 0.f, .01f))
		{
			MeshRotation.Yaw = GuardMeshYawOffset;
			GetMesh()->SetRelativeRotation(MeshRotation);
		}
	}
	bRotateRootBone = false;
	AO_Yaw = 0;
	InterpAO_Yaw = 0;
	TurningInPlace = ETurningInPlace::ETIP_NotTurning;
}

void ABrokenEdgeCharacter::RotateInPlace(float DeltaTime)
{
	if (bExtractionGuard)
	{
		// 装备同步也可能改写转向标志，因此每帧恢复守卫策略，并跳过后面的玩家 AimOffset/TurnInPlace。
		ConfigureGuardRotation();
		CalculateAO_Pitch();
		return;
	}
	if (IsTraversing())
	{
		bUseControllerRotationYaw = false;
		TurningInPlace = ETurningInPlace::ETIP_NotTurning;
		return;
	}
	if (Combat && Combat->bHoldingTheFlag)
	{
		bUseControllerRotationYaw = false;
		GetCharacterMovement()->bOrientRotationToMovement = true;
		TurningInPlace = ETurningInPlace::ETIP_NotTurning;
		return;
	}

	if (Combat && Combat->EquippedWeapon) GetCharacterMovement()->bOrientRotationToMovement = false;
	if (Combat && Combat->EquippedWeapon) bUseControllerRotationYaw = true;

	if (bDisableGameplay)
	{
		bUseControllerRotationYaw = false;
		TurningInPlace = ETurningInPlace::ETIP_NotTurning;
		return;
	}

	if (GetLocalRole() > ENetRole::ROLE_SimulatedProxy && IsLocallyControlled())
	{
		AimOffset(DeltaTime);
	}
	else
	{
		TimeSinceLastMovementReplication += DeltaTime;
		if (TimeSinceLastMovementReplication > 0.25f)
		{
			OnRep_ReplicatedMovement();
		}
		CalculateAO_Pitch();
	}
}

/**
 * @brief 计算角色移动速度
 * @return 水平方向速度大小
 */
float ABrokenEdgeCharacter::CalculateSpeed()
{
	FVector Velocity = GetVelocity();
	Velocity.Z = 0.f;
	return Velocity.Size();
}

/**
 * @brief 计算瞄准偏移
 * @param DeltaTime 帧时间
 */
void ABrokenEdgeCharacter::AimOffset(float DeltaTime)
{
	if (Combat && Combat->EquippedWeapon == nullptr) return;
	float Speed = CalculateSpeed();
	bool bIsInAir = GetCharacterMovement()->IsFalling();

	if (Speed == 0.f && !bIsInAir) // 静止不动，不跳跃
	{
		bRotateRootBone = true;
		FRotator CurrentAimRotation = FRotator(0.f, GetBaseAimRotation().Yaw, 0.f);
		FRotator DeltaAimRotation = UKismetMathLibrary::NormalizedDeltaRotator(CurrentAimRotation, StartingAimRotation);
		AO_Yaw = DeltaAimRotation.Yaw;
		if (TurningInPlace == ETurningInPlace::ETIP_NotTurning)
		{
			InterpAO_Yaw = AO_Yaw;
		}
		bUseControllerRotationYaw = true;
		TurnInPlace(DeltaTime);
	}

	if (Speed > 0.f || bIsInAir) // 跑或者跳
	{
		bRotateRootBone = false;
		StartingAimRotation = FRotator(0.f, GetBaseAimRotation().Yaw, 0.f);
		AO_Yaw = 0.f;
		bUseControllerRotationYaw = true;
		TurningInPlace = ETurningInPlace::ETIP_NotTurning;
	}

	CalculateAO_Pitch();
}

/**
 * @brief 计算瞄准俯仰角
 */
void ABrokenEdgeCharacter::CalculateAO_Pitch()
{
	AO_Pitch = GetBaseAimRotation().Pitch;
	if (AO_Pitch > 90.f && !IsLocallyControlled())
	{
		// 将map俯仰角从 [270, 360) 转换为 [-90, 0)
		FVector2D InRange(270.f, 360.f);
		FVector2D OutRange(-90.f, 0.f);
		AO_Pitch = FMath::GetMappedRangeValueClamped(InRange, OutRange, AO_Pitch);
	}
}

/**
 * @brief 原地转向插值处理
 * @param DeltaTime 帧时间
 */
void ABrokenEdgeCharacter::TurnInPlace(float DeltaTime)
{
	if (AO_Yaw > 90.f)
	{
		TurningInPlace = ETurningInPlace::ETIP_Right;
	}
	else if (AO_Yaw < -90.f)
	{
		TurningInPlace = ETurningInPlace::ETIP_Left;
	}
	if (TurningInPlace != ETurningInPlace::ETIP_NotTurning)
	{
		InterpAO_Yaw = FMath::FInterpTo(InterpAO_Yaw, 0.f, DeltaTime, 4.f);
		AO_Yaw = InterpAO_Yaw;
		if (FMath::Abs(AO_Yaw) < 15.f)
		{
			TurningInPlace = ETurningInPlace::ETIP_NotTurning;
			StartingAimRotation = FRotator(0.f, GetBaseAimRotation().Yaw, 0.f);
		}
	}
}

void ABrokenEdgeCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	bFirstPersonInitializationFailed = false;
	if (!IsLocallyControlled())
	{
		ReleaseFirstPersonView();
	}
	RefreshFirstPersonWorldWeapons();

	// Add Input Mapping Context
	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<
			UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}
}

// Called to bind functionality to input
void ABrokenEdgeCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	PlayerInputComponent->BindAction(TEXT("HoldBreath"), IE_Pressed, ExtractionCombat.Get(),
	                                 &UExtractionCombatComponent::HoldBreathPressed);
	PlayerInputComponent->BindAction(TEXT("HoldBreath"), IE_Released, ExtractionCombat.Get(),
	                                 &UExtractionCombatComponent::HoldBreathReleased);
	// F 键开门：独立于 Enhanced Input 的 IA_Equip，避免抢占换枪键位。
	PlayerInputComponent->BindAction(TEXT("InteractDoor"), IE_Pressed, this,
	                                 &ABrokenEdgeCharacter::InteractDoorPressed);
	Super::SetupPlayerInputComponent(PlayerInputComponent);


	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (bEnableSprint && SprintAction)
		{
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Started, this,
			                                   &ABrokenEdgeCharacter::SprintPressed);
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Completed, this,
			                                   &ABrokenEdgeCharacter::SprintReleased);
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Canceled, this,
			                                   &ABrokenEdgeCharacter::SprintReleased);
		}


		//跳跃
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ABrokenEdgeCharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this,
		                                   &ABrokenEdgeCharacter::StopJumping);

		//移动
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ABrokenEdgeCharacter::Move);

		//看
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ABrokenEdgeCharacter::Look);

		//蹲伏
		EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Started, this,
		                                   &ABrokenEdgeCharacter::CrouchButtonPressed);

		//装备武器
		EnhancedInputComponent->BindAction(EquipAction, ETriggerEvent::Started, this,
		                                   &ABrokenEdgeCharacter::EquipButtonPressed);

		//瞄准动作
		EnhancedInputComponent->BindAction(AimAction, ETriggerEvent::Started, this,
		                                   &ABrokenEdgeCharacter::AimButtonPressed);
		EnhancedInputComponent->BindAction(AimAction, ETriggerEvent::Completed, this,
		                                   &ABrokenEdgeCharacter::AimButtonReleased);

		//换子弹
		EnhancedInputComponent->BindAction(ReloadAction, ETriggerEvent::Started, this,
		                                   &ABrokenEdgeCharacter::ReloadButtonPressed);

		//丢弃武器
		EnhancedInputComponent->BindAction(DropAction, ETriggerEvent::Started, this,
		                                   &ABrokenEdgeCharacter::DropButtonPressed);

		//开火动作
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this,
		                                   &ABrokenEdgeCharacter::FireButtonPressed);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Completed, this,
		                                   &ABrokenEdgeCharacter::FireButtonReleased);
	}
	else
	{
		UE_LOG(LogTemp, Error,
		       TEXT(
			       "'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."
		       ), *GetNameSafe(this));
	}
}

void ABrokenEdgeCharacter::InitializeFirstPersonView()
{
	if (bFirstPersonView || bFirstPersonInitializationFailed || !HasActorBegunPlay() ||
		!IsPlayerControlled() || !IsLocallyControlled() || bExtractionGuard || bDefeated || bDisableGameplay)
		return;
	if (!FollowCamera || !GetMesh() || !GetMesh()->IsRegistered()) return;

	// 明确的资源错误只报告一次；重新获得控制权时允许再次初始化。
	if (!CreateFirstPersonBody())
	{
		bFirstPersonInitializationFailed = true;
		return;
	}
	bOriginalMeshOwnerNoSee = GetMesh()->bOwnerNoSee;
	OriginalBodyPrimitiveType = GetMesh()->FirstPersonPrimitiveType;
	bOriginalMeshCastHiddenShadow = GetMesh()->bCastHiddenShadow;
	OriginalMeshAnimTickOption = GetMesh()->VisibilityBasedAnimTickOption;

	// 同时覆盖旧蓝图曾保存的相机挂载与控制旋转设置。
	FollowCamera->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	FollowCamera->bUsePawnControlRotation = true;
	UpdateFirstPersonCameraLocation();
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	GetMesh()->SetCastHiddenShadow(true);
	// 世界人物仍负责动画通知、武器挂点和远端表现。
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	bFirstPersonView = true;
	UpdateFirstPersonWeapon();
	RefreshFirstPersonWorldWeapons();
	Melee->RefreshPresentation();
}

void ABrokenEdgeCharacter::ReleaseFirstPersonView()
{
	bFirstPersonInitializationFailed = false;
	if (!bFirstPersonView) return;
	// 先清除本地表现状态，再恢复世界武器可见性；不切换到其他相机。
	bFirstPersonView = false;
	FirstPersonSourceWeapon = nullptr;
	Melee->ReleaseViewMesh();
	if (FirstPersonWeaponMesh)
	{
		FirstPersonWeaponMesh->DestroyComponent();
		FirstPersonWeaponMesh = nullptr;
	}
	if (DedicatedFirstPersonMesh)
	{
		DedicatedFirstPersonMesh->UnregisterOnBoneTransformsFinalizedDelegate(ViewWeaponPoseDelegateHandle);
		ViewWeaponPoseDelegateHandle.Reset();
		DedicatedFirstPersonMesh->DestroyComponent();
		DedicatedFirstPersonMesh = nullptr;
	}
	GetMesh()->UnregisterOnBoneTransformsFinalizedDelegate(FirstPersonPoseDelegateHandle);
	FirstPersonPoseDelegateHandle.Reset();
	if (FirstPersonBody)
	{
		FirstPersonBody->DestroyComponent();
		FirstPersonBody = nullptr;
	}
	// 保留尸体、失去控制后的角色及远端观察者需要的世界人物表现。
	GetMesh()->SetOwnerNoSee(bOriginalMeshOwnerNoSee);
	GetMesh()->SetFirstPersonPrimitiveType(OriginalBodyPrimitiveType);
	GetMesh()->SetCastHiddenShadow(bOriginalMeshCastHiddenShadow);
	GetMesh()->VisibilityBasedAnimTickOption = OriginalMeshAnimTickOption;
	RefreshFirstPersonWorldWeapons();
	Melee->RefreshPresentation();
}

void ABrokenEdgeCharacter::TrackFirstPersonWorldWeapon(AWeapon* Weapon)
{
	if (IsValid(Weapon)) FirstPersonWorldWeapons.Add(Weapon);
}

void ABrokenEdgeCharacter::RefreshFirstPersonWorldWeapons()
{
	if (Combat)
	{
		if (IsValid(Combat->EquippedWeapon)) Combat->EquippedWeapon->RefreshOwnerViewVisibility();
		if (IsValid(Combat->SecondaryWeapon)) Combat->SecondaryWeapon->RefreshOwnerViewVisibility();
	}
	// Retain swapped weapons, and revisit removed slots without keeping actors alive.
	for (const auto WeakWeapon : FirstPersonWorldWeapons.Array())
	{
		auto Weapon = WeakWeapon.Get();
		if (Weapon) Weapon->RefreshOwnerViewVisibility();
		if (!Weapon || Weapon->GetOwner() != this || !bFirstPersonView || !IsLocallyControlled())
			FirstPersonWorldWeapons.Remove(WeakWeapon);
	}
}

bool ABrokenEdgeCharacter::CreateFirstPersonBody()
{
	if (FirstPersonMeshAsset && FirstPersonAnimClass)
	{
		DedicatedFirstPersonMesh = NewObject<USkeletalMeshComponent>(this);
		DedicatedFirstPersonMesh->SetupAttachment(FollowCamera);
		DedicatedFirstPersonMesh->SetSkeletalMeshAsset(FirstPersonMeshAsset);
		DedicatedFirstPersonMesh->SetAnimInstanceClass(FirstPersonAnimClass);
		DedicatedFirstPersonMesh->SetRelativeTransform(FirstPersonMeshTransform);
		DedicatedFirstPersonMesh->SetOnlyOwnerSee(true);
		DedicatedFirstPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
		DedicatedFirstPersonMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DedicatedFirstPersonMesh->SetGenerateOverlapEvents(false);
		DedicatedFirstPersonMesh->SetCastShadow(false);
		DedicatedFirstPersonMesh->SetCanEverAffectNavigation(false);
		DedicatedFirstPersonMesh->VisibilityBasedAnimTickOption =
			EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		DedicatedFirstPersonMesh->RegisterComponent();
		DedicatedFirstPersonMesh->HideBoneByName(FirstPersonHeadBone, PBO_None);
		for (const FName Slot : FirstPersonHiddenMaterialSlots)
		{
			const int32 MaterialIndex = DedicatedFirstPersonMesh->GetMaterialIndex(Slot);
			if (MaterialIndex != INDEX_NONE)
				for (int32 LOD = 0; LOD < DedicatedFirstPersonMesh->GetNumLODs(); ++LOD)
					DedicatedFirstPersonMesh->ShowMaterialSection(MaterialIndex, INDEX_NONE, false, LOD);
		}
		ViewWeaponPoseDelegateHandle = DedicatedFirstPersonMesh->RegisterOnBoneTransformsFinalizedDelegate(
			FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(
				this, &ABrokenEdgeCharacter::UpdateFirstPersonWeaponPose));
		return true;
	}
	if (!GetMesh()->GetSkeletalMeshAsset() || GetMesh()->GetBoneIndex(FirstPersonHeadBone) == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("Cannot enter first person: head bone '%s' is missing on %s."),
		       *FirstPersonHeadBone.ToString(), *GetName());
		return false;
	}

	FirstPersonBody = NewObject<UPoseableMeshComponent>(this);
	FirstPersonBody->SetupAttachment(GetMesh());
	FirstPersonBody->SetSkinnedAssetAndUpdate(GetMesh()->GetSkinnedAsset());
	for (int32 MaterialIndex = 0; MaterialIndex < GetMesh()->GetNumMaterials(); ++MaterialIndex)
	{
		FirstPersonBody->SetMaterial(MaterialIndex, GetMesh()->GetMaterial(MaterialIndex));
	}
	FirstPersonBody->SetOnlyOwnerSee(true);
	FirstPersonBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonBody->SetGenerateOverlapEvents(false);
	FirstPersonBody->SetCastShadow(false);
	FirstPersonBody->SetCanEverAffectNavigation(false);
	FirstPersonBody->RegisterComponent();
	FirstPersonBody->SetComponentTickEnabled(false);
	FirstPersonBody->HideBoneByName(FirstPersonHeadBone, PBO_None);

	// Copy only after animation/IK finishes, so the visible hands match the weapon sockets.
	FirstPersonPoseDelegateHandle = GetMesh()->RegisterOnBoneTransformsFinalizedDelegate(
		FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this,
		                                                              &ABrokenEdgeCharacter::UpdateFirstPersonBodyPose));
	UpdateFirstPersonBodyPose();
	return true;
}

void ABrokenEdgeCharacter::SetLadderBodyView(bool bOn)
{
	if (!IsLocallyControlled() || !bFirstPersonView || !DedicatedFirstPersonMesh) return;
	if (bOn)
	{
		GetMesh()->SetOwnerNoSee(false);
		// 世界表示图元在拥有者第一人称画面里会被隐藏，爬梯期间临时当普通图元渲染。
		GetMesh()->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::None);
		// 从脖子起隐藏（没有 Neck 骨骼才只藏头），否则低头会看到头被藏后留下的脖子断口和模型内壁。
		LadderHiddenBone = GetMesh()->GetBoneIndex(TEXT("Neck")) != INDEX_NONE ? FName(TEXT("Neck")) : FirstPersonHeadBone;
		GetMesh()->HideBoneByName(LadderHiddenBone, PBO_None);
	}
	else
	{
		GetMesh()->UnHideBoneByName(LadderHiddenBone.IsNone() ? FirstPersonHeadBone : LadderHiddenBone);
		GetMesh()->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
		GetMesh()->SetOwnerNoSee(true);
	}
	DedicatedFirstPersonMesh->SetHiddenInGame(bOn);
	if (FirstPersonWeaponMesh) FirstPersonWeaponMesh->SetHiddenInGame(bOn);
	bLadderBodyView = bOn;
}

void ABrokenEdgeCharacter::UpdateFirstPersonBodyPose()
{
	if (!FirstPersonBody || !GetMesh()) return;
	FirstPersonBody->CopyPoseFromSkeletalComponent(GetMesh());
	FirstPersonBody->RefreshBoneTransforms();
}

void ABrokenEdgeCharacter::UpdateFirstPersonCameraLocation()
{
	// BaseEyeHeight is updated by ACharacter when entering or leaving crouch.
	const FRotator ViewYaw(0.f, GetViewRotation().Yaw, 0.f);
	FVector Target = GetPawnViewLocation() + ViewYaw.RotateVector(FirstPersonCameraOffset);
	// 爬梯显示整个身体时，爬梯动作会把身体往梯子推几十厘米，相机仍在胶囊眼高就会落进脖子和后背里。
	// 做法同“真第一人称”游戏：相机跟着头骨，放在脸前一点；头和脖子已隐藏。
	if (bLadderBodyView && GetMesh())
	{
		const FName Bone = GetMesh()->GetBoneIndex(FirstPersonHeadBone) != INDEX_NONE ? FirstPersonHeadBone : FName(TEXT("Head"));
		if (GetMesh()->GetBoneIndex(Bone) != INDEX_NONE)
			Target = GetMesh()->GetBoneLocation(Bone) + GetActorForwardVector() * 14.f + FVector(0, 0, 6.f);
	}
	const bool bBlend = bLadderBodyView || LadderCameraBlend > 0.f;
	LadderCameraBlend = bLadderBodyView ? .25f : FMath::Max(0.f, LadderCameraBlend - GetWorld()->GetDeltaSeconds());
	FollowCamera->SetWorldLocation(bBlend
		? FMath::VInterpTo(FollowCamera->GetComponentLocation(), Target, GetWorld()->GetDeltaSeconds(), 20.f)
		: Target);
}

void ABrokenEdgeCharacter::UpdateFirstPersonWeapon()
{
	// 先刷新世界武器对拥有者的可见性；没有专用第一人称人物网格时不更新本地枪模。
	RefreshFirstPersonWorldWeapons();
	if (!DedicatedFirstPersonMesh) return;
	// 未启用武器适配时，用人物网格偏移表现瞄准；启用适配后由枪械姿势处理，避免重复偏移。
	const FVector AimOffset = GetViewWeaponFit().bEnabled
		                          ? FVector::ZeroVector
		                          : (IsAiming() ? FirstPersonAimMeshOffset : FVector::ZeroVector);
	// 切枪放下：手和枪（枪跟右手走）一起往下、往后退出画面，单独叠加，不参与插值。
	const FVector SwapOffset = FVector(-12.f, 0.f, -42.f) * SyncFirstPersonSwap();
	DedicatedFirstPersonMesh->SetRelativeLocation(FMath::VInterpTo(
		DedicatedFirstPersonMesh->GetRelativeLocation() - ViewSwapOffsetApplied,
		FirstPersonMeshTransform.GetLocation() + AimOffset, GetWorld()->GetDeltaSeconds(),
		FirstPersonAimInterpSpeed) + SwapOffset);
	ViewSwapOffsetApplied = SwapOffset;
	AWeapon* Weapon = GetEquippedWeapon();
	// 持有者或掉落状态的复制可能先于装备槽清理到达，不能继续把这类武器当作当前持枪。
	if (!IsValid(Weapon) || Weapon->GetOwner() != this || Weapon->GetWeaponState() == EWeaponState::EWS_Dropped)
		Weapon = nullptr;
	// 仅在装备来源改变时重新配置显示副本，避免每帧重复创建组件或设置资源。
	if (FirstPersonSourceWeapon != Weapon)
	{
		FirstPersonSourceWeapon = Weapon;
		if (Weapon)
		{
			// 按需创建本地枪模：适配模式挂到相机，否则挂到专用人物的右手插槽。
			if (!FirstPersonWeaponMesh)
			{
				FirstPersonWeaponMesh = NewObject<USkeletalMeshComponent>(this);
				FirstPersonWeaponMesh->SetupAttachment(GetViewWeaponFit().bEnabled
					                                       ? static_cast<USceneComponent*>(FollowCamera)
					                                       : static_cast<USceneComponent*>(DedicatedFirstPersonMesh),
				                                       GetViewWeaponFit().bEnabled
					                                       ? NAME_None
					                                       : FName(TEXT("RightHandSocket")));
				// 该组件只负责拥有者的第一人称表现，不参与碰撞、重叠、阴影或导航。
				FirstPersonWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				FirstPersonWeaponMesh->SetGenerateOverlapEvents(false);
				FirstPersonWeaponMesh->SetOnlyOwnerSee(true);
				FirstPersonWeaponMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
				FirstPersonWeaponMesh->SetCastShadow(false);
				FirstPersonWeaponMesh->SetCanEverAffectNavigation(false);
				FirstPersonWeaponMesh->RegisterComponent();
			}
			// 网格优先使用武器表现配置；其次使用来源匹配的角色级替代网格，最后复用世界武器网格。
			USkeletalMesh* SourceMesh = Weapon->GetWeaponMesh()->GetSkeletalMeshAsset();
			FirstPersonWeaponMesh->SetSkeletalMeshAsset(
				Weapon->Presentation && Weapon->Presentation->ViewMesh
					? Weapon->Presentation->ViewMesh.Get()
					: FirstPersonWeaponVisualMesh && SourceMesh == FirstPersonWeaponSourceMesh
					? FirstPersonWeaponVisualMesh.Get()
					: SourceMesh);
			// 清除上一把枪的材质覆盖；专用表现网格使用自身材质，其余路径复制世界武器材质。
			FirstPersonWeaponMesh->EmptyOverrideMaterials();
			if (!(Weapon->Presentation && Weapon->Presentation->ViewMesh))
			{
				for (int32 Index = 0; Index < Weapon->GetWeaponMesh()->GetNumMaterials(); ++Index)
					FirstPersonWeaponMesh->SetMaterial(Index, Weapon->GetWeaponMesh()->GetMaterial(Index));
			}
			// 切换武器后重设基础变换，并按当前武器是否实际启用适配重新确定挂载位置。
			FirstPersonWeaponMesh->SetRelativeTransform(FirstPersonWeaponTransform);
			FirstPersonWeaponMesh->AttachToComponent(UsesFirstPersonWeaponFit()
				                                         ? static_cast<USceneComponent*>(FollowCamera)
				                                         : static_cast<USceneComponent*>(DedicatedFirstPersonMesh),
			                                         FAttachmentTransformRules::KeepRelativeTransform,
			                                         UsesFirstPersonWeaponFit()
				                                         ? NAME_None
				                                         : FName(TEXT("RightHandSocket")));
			// 适配模式使用专用枪械动画实例；让枪模在人物网格之后更新，再从腰射姿势开始。
			FirstPersonWeaponMesh->SetAnimInstanceClass(UsesFirstPersonWeaponFit()
				                                            ? UStingerFirstPersonWeaponAnimInstance::StaticClass()
				                                            : nullptr);
			FirstPersonWeaponMesh->AddTickPrerequisiteComponent(DedicatedFirstPersonMesh);
			ViewWeaponTarget = GetViewWeaponFit().Hip;
		}
	}
	// 适配模式在腰射与瞄准变换之间更新目标姿势，并纳入武器装配产生的瞄准偏移。
	if (UsesFirstPersonWeaponFit())
	{
		FTransform Target = IsAiming() ? GetViewWeaponFit().Aim : GetViewWeaponFit().Hip;
		if (IsAiming() && Weapon) Target.AddToTranslation(Weapon->EquipmentAimOffset());
		Target.AddToTranslation(SwapOffset);
		// 撤离战斗使用组件提供的瞄准进度混合姿势，再叠加视觉后坐力旋转。
		if (ExtractionCombat->IsEnabled())
		{
			FTransform Aim = GetViewWeaponFit().Aim;
			if (Weapon) Aim.AddToTranslation(Weapon->EquipmentAimOffset());
			ViewWeaponTarget.Blend(GetViewWeaponFit().Hip, Aim, ExtractionCombat->GetAimAlpha());
			ViewWeaponTarget.AddToTranslation(SwapOffset);
			ViewWeaponTarget.SetRotation(
				ViewWeaponTarget.GetRotation() * ExtractionCombat->GetVisualKick().Quaternion());
		}
		else
		{
			// 其他战斗路径分别插值位置与旋转，使枪械平滑靠近当前目标姿势。
			ViewWeaponTarget.SetLocation(FMath::VInterpTo(ViewWeaponTarget.GetLocation(), Target.GetLocation(),
			                                              GetWorld()->GetDeltaSeconds(), FirstPersonAimInterpSpeed));
			ViewWeaponTarget.SetRotation(FMath::QInterpTo(ViewWeaponTarget.GetRotation(), Target.GetRotation(),
			                                              GetWorld()->GetDeltaSeconds(), FirstPersonAimInterpSpeed));
		}
	}
	// 翻越期间不执行常规第一人称动作同步，避免干扰翻越表现。
	if (!IsTraversing()) SyncFirstPersonAction();
	// 枪模只在有效持枪、未死亡且非翻越和近战时显示；人物网格还可服务于近战或翻越。
	const bool bScope = IsFirstPersonScopeActive();
	if (FirstPersonWeaponMesh)
		FirstPersonWeaponMesh->SetVisibility(
			Weapon && !bDefeated && !IsTraversing() && !Melee->Active && !bScope, true);
	DedicatedFirstPersonMesh->SetVisibility((Weapon || Melee->Active || IsTraversing()) && !bDefeated && !bScope,
	                                        false);
}

bool ABrokenEdgeCharacter::IsFirstPersonScopeActive() const
{
	const auto* Weapon = Combat ? Combat->EquippedWeapon : nullptr;
	return IsLocallyControlled() && IsFirstPersonView() && Weapon && Weapon->Presentation
		&& Weapon->Presentation->ScopeOverlay && Combat->bAiming && !bDefeated && !Melee->Active
		&& !IsTraversing() && !IsSprinting() && !ExtractionInteraction->InventoryOpen
		&& GetCombatState() == ECombatState::ECS_Unoccuiped
		&& (!ExtractionCombat->IsEnabled() || ExtractionCombat->GetAimAlpha() >= .98f);
}

bool ABrokenEdgeCharacter::UsesFirstPersonWeaponFit() const
{
	return !Melee->Active && !IsTraversing() && GetViewWeaponFit().bEnabled && FirstPersonSourceWeapon &&
		FirstPersonWeaponMesh &&
		(FirstPersonSourceWeapon->Presentation || FirstPersonSourceWeapon->GetWeaponMesh()->GetSkeletalMeshAsset() ==
			FirstPersonWeaponSourceMesh);
}

FTransform ABrokenEdgeCharacter::GetFirstPersonGripTarget(FName Socket) const
{
	if (!UsesFirstPersonWeaponFit() || !FirstPersonWeaponMesh->DoesSocketExist(Socket)) return FTransform::Identity;
	if (Socket == TEXT("LeftHandSocket") && IsValid(FirstPersonSourceWeapon))
		return FirstPersonSourceWeapon->EquipmentGrip(true) * ViewWeaponTarget * FollowCamera->GetComponentTransform();
	return FirstPersonWeaponMesh->GetSocketTransform(Socket, RTS_Component) * ViewWeaponTarget * FollowCamera->
		GetComponentTransform();
}

void ABrokenEdgeCharacter::UpdateFirstPersonWeaponPose()
{
	if (!UsesFirstPersonWeaponFit() || !DedicatedFirstPersonMesh->DoesSocketExist(TEXT("RightHand"))) return;
	// Update after final IK, so the gun and wrist share the same rendered pose.
	FirstPersonWeaponMesh->SetWorldTransform(
		FirstPersonWeaponMesh->GetSocketTransform(TEXT("Hand_R"), RTS_Component).Inverse()
		* DedicatedFirstPersonMesh->GetSocketTransform(TEXT("RightHand")));
}

void ABrokenEdgeCharacter::PlayFirstPersonMontage(UAnimMontage* Montage, UAnimMontage* ThirdPersonMontage,
                                                  FName Section)
{
	ViewActionMontage = Montage;
	ViewActionThirdPersonMontage = ThirdPersonMontage;
	ViewActionSectionStart = 0.f;
	ViewActionSectionLength = ThirdPersonMontage ? ThirdPersonMontage->GetPlayLength() : 1.f;
	ViewActionAttachTime = -1.f;
	if (ThirdPersonMontage && !Section.IsNone())
	{
		const int32 Index = ThirdPersonMontage->GetSectionIndex(Section);
		if (Index != INDEX_NONE)
		{
			float End;
			ThirdPersonMontage->GetSectionStartAndEndTime(Index, ViewActionSectionStart, End);
			ViewActionSectionLength = End - ViewActionSectionStart;
		}
	}
	if (ThirdPersonMontage)
	{
		const FString Finish = Montage == GetViewReloadMontage()
			                       ? TEXT("ReloadFinished")
			                       : Montage == GetViewSwapMontage()
			                       ? TEXT("SwapFinished")
			                       : TEXT("");
		for (const FAnimNotifyEvent& Notify : ThirdPersonMontage->Notifies)
		{
			const float Time = Notify.GetTriggerTime() - ViewActionSectionStart;
			if (Time <= 0.f || Time > ViewActionSectionLength) continue;
			const FString Name = Notify.GetNotifyEventName().ToString();
			if (!Finish.IsEmpty() && Name.Contains(Finish)) ViewActionSectionLength = Time;
			if (Montage == GetViewSwapMontage() && Name.Contains(TEXT("AttachWeapons")))
				ViewActionAttachTime = Time;
		}
	}
	SyncFirstPersonAction();
}

void ABrokenEdgeCharacter::SyncFirstPersonAction()
{
	if (!DedicatedFirstPersonMesh || !ViewActionMontage || !ViewActionThirdPersonMontage) return;
	UAnimInstance* Body = GetMesh()->GetAnimInstance();
	UAnimInstance* View = DedicatedFirstPersonMesh->GetAnimInstance();
	if (!Body || !View) return;
	if (bDefeated || !Body->Montage_IsPlaying(ViewActionThirdPersonMontage))
	{
		if (View->Montage_IsPlaying(ViewActionMontage)) View->Montage_Stop(.1f, ViewActionMontage);
		return;
	}
	if (Body->Montage_GetPosition(ViewActionThirdPersonMontage) - ViewActionSectionStart >= ViewActionSectionLength)
	{
		if (View->Montage_IsPlaying(ViewActionMontage)) View->Montage_Stop(.1f, ViewActionMontage);
		return;
	}
	float Fraction = FMath::Clamp(
		(Body->Montage_GetPosition(ViewActionThirdPersonMontage) - ViewActionSectionStart)
		/ FMath::Max(ViewActionSectionLength, .01f), 0.f, .999f);
	if (ViewActionAttachTime > 0.f && ViewActionAttachTime < ViewActionSectionLength)
	{
		const float Key = ViewActionAttachTime / ViewActionSectionLength;
		Fraction = Fraction < Key
			           ? Fraction / Key * GetViewWeaponFit().SwapAttachFraction
			           : GetViewWeaponFit().SwapAttachFraction + (Fraction - Key) / (1.f - Key)
			           * (1.f - GetViewWeaponFit().SwapAttachFraction);
	}
	const float Position = Fraction * ViewActionMontage->GetPlayLength();
	if (!View->Montage_IsPlaying(ViewActionMontage))
		View->Montage_Play(ViewActionMontage,
		                   ViewActionMontage->GetPlayLength() / FMath::Max(ViewActionSectionLength, .01f),
		                   EMontagePlayReturnType::MontageLength, Position);
	View->Montage_SetPosition(ViewActionMontage, Position);
}

float ABrokenEdgeCharacter::GetFirstPersonReloadProgress() const
{
	if (!ViewActionMontage || ViewActionMontage != GetViewReloadMontage() || !ViewActionThirdPersonMontage)
		return -
			1.f;
	const UAnimInstance* Body = GetMesh()->GetAnimInstance();
	if (!Body || !Body->Montage_IsPlaying(ViewActionThirdPersonMontage) || bDefeated) return -1.f;
	return FMath::Clamp((Body->Montage_GetPosition(ViewActionThirdPersonMontage) - ViewActionSectionStart)
	                    / FMath::Max(ViewActionSectionLength, .01f), 0.f, 1.f);
}

void ABrokenEdgeCharacter::SprintPressed()
{
	if (!bEnableSprint) return;
	bSprintRequested = true;
	ServerSetSprintRequested(true);
}

void ABrokenEdgeCharacter::SprintReleased()
{
	bSprintRequested = false;
	ServerSetSprintRequested(false);
}

void ABrokenEdgeCharacter::ServerSetSprintRequested_Implementation(bool bRequested)
{
	if (bRequested && ExtractionInteraction->IsEnabled()) ExtractionInteraction->CancelMedical();
	bSprintRequested = bEnableSprint && bRequested;
}

void ABrokenEdgeCharacter::UpdateExtractionLabel()
{
	if (GetNetMode() == NM_DedicatedServer || !ExtractionCombat->IsEnabled()) return;
	auto* PC = GetWorld()->GetFirstPlayerController();
	auto* Viewer = PC ? Cast<ABrokenEdgeCharacter>(PC->GetPawn()) : nullptr;
	const bool Ally = Viewer && Viewer != this && UExtractionAbilities::Friendly(Viewer, this);
	if ((!Ally && !bExtractionGuard) || !PC || bDefeated)
	{
		ExtractionLabel->SetVisibility(false);
		return;
	}
	FVector Eye;
	FRotator Rotation;
	PC->GetPlayerViewPoint(Eye, Rotation);
	const FVector Point = GetActorLocation() + FVector(0, 0, 30);
	FCollisionQueryParams Q(SCENE_QUERY_STAT(ExtractionLabel), false, this);
	Q.AddIgnoredActor(Viewer);
	if (Viewer && Viewer->GetEquippedWeapon()) Q.AddIgnoredActor(Viewer->GetEquippedWeapon());
	if (GetEquippedWeapon()) Q.AddIgnoredActor(GetEquippedWeapon());
	FHitResult Hit;
	const bool Visible = FVector::DistSquared(Eye, Point) < FMath::Square(3000.f)
		&& !GetWorld()->LineTraceSingleByChannel(Hit, Eye, Point, ECC_Visibility, Q)
		&& !AExtractionSmoke::Blocks(GetWorld(), Eye, Point);
	ExtractionLabel->SetVisibility(Visible);
	if (!Visible) return;
	const auto* PS = GetPlayerState<AExtractionPlayerState>();
	// 守卫头顶：交战时红色“!”，搜索时琥珀色“?”，平时显示 GUARD。
	ExtractionLabel->SetText(FText::FromString(bExtractionGuard
		                                           ? (ExtractionAlert == 2 ? TEXT("!") : ExtractionAlert == 1 ? TEXT("?") : TEXT("GUARD"))
		                                           : PS && PS->Life == EExtractionLife::Downed
		                                           ? TEXT("REVIVE [E]")
		                                           : TEXT("SQUADMATE")));
	ExtractionLabel->SetTextRenderColor(!bExtractionGuard
		                                    ? FColor(80, 220, 180)
		                                    : ExtractionAlert == 2
		                                    ? FColor(234, 90, 80)
		                                    : ExtractionAlert == 1
		                                    ? FColor(242, 169, 59)
		                                    : FColor(230, 150, 80));
	ExtractionLabel->SetWorldRotation((Eye - ExtractionLabel->GetComponentLocation()).Rotation());
}

void ABrokenEdgeCharacter::ToggleProne()
{
	if (!ExtractionCombat->IsEnabled() || IsTraversing() || bDisableGameplay || ExtractionAbilities->IsDowned()) return;
	bProne = !bProne;
	if (!HasAuthority()) ServerSetProne(bProne);
}

void ABrokenEdgeCharacter::ServerSetProne_Implementation(bool bNewProne)
{
	bProne = bNewProne && ExtractionCombat->IsEnabled() && !ExtractionAbilities->IsDowned();
}

void ABrokenEdgeCharacter::ApplyProne(bool bNewProne)
{
	if (bNewProne == bProneApplied) return;
	auto* Movement = GetCharacterMovement();
	if (bNewProne)
	{
		ProneSavedCrouchSpeed = Movement->MaxWalkSpeedCrouched;
		ProneSavedEyeHeight = CrouchedEyeHeight;
		Movement->MaxWalkSpeedCrouched = GetDefault<UExtractionRules>()->ProneSpeed;
		CrouchedEyeHeight = GetDefault<UExtractionRules>()->ProneEyeHeight;
		Crouch();
	}
	else
	{
		Movement->MaxWalkSpeedCrouched = ProneSavedCrouchSpeed;
		CrouchedEyeHeight = ProneSavedEyeHeight;
		UnCrouch();
	}
	// 已经蹲着时 Crouch/UnCrouch 不会重算视线，这里直接同步。
	if (bIsCrouched) BaseEyeHeight = CrouchedEyeHeight;
	bProneApplied = bNewProne;
}

UAnimSequence* ABrokenEdgeCharacter::ProneAnim(const TSoftObjectPtr<UAnimSequence>& Anim) const
{
	if (!bProneAnimating || Anim.IsNull()) return nullptr;
	return Anim.LoadSynchronous();
}

void ABrokenEdgeCharacter::UpdateProneAnimation()
{
	auto* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim) return;
	const bool bWant = bProne && !ExtractionAbilities->IsDowned() && !IsTraversing() && !bDefeated;
	const double Now = GetWorld()->GetTimeSeconds();
	if (bWant != bProneAnimating)
	{
		bProneAnimating = bWant;
		if (ProneMontage) Anim->Montage_Stop(.2f, ProneMontage);
		ProneMontage = nullptr;
		if (bWant)
		{
			if (auto* Down = StandToProneAnim.LoadSynchronous())
			{
				ProneMontage = Anim->PlaySlotAnimationAsDynamicMontage(Down, TEXT("DefaultSlot"), .15f, .25f, 1.f, 1);
				ProneIdleAt = Now + FMath::Max(.1f, Down->GetPlayLength() - .25f);
			}
			else ProneIdleAt = Now;
		}
		else if (auto* Up = ProneToStandAnim.LoadSynchronous())
			Anim->PlaySlotAnimationAsDynamicMontage(Up, TEXT("DefaultSlot"), .1f, .25f, 1.f, 1);
		return;
	}
	if (!bProneAnimating || Now < ProneIdleAt) return;
	// 过渡播完、或被开火 / 换弹等蒙太奇打断且已结束时，循环趴姿待机。
	const bool bIdlePlaying = ProneMontage && Anim->Montage_IsPlaying(ProneMontage)
		&& ProneMontage->SlotAnimTracks.Num() > 0 && ProneMontage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() > 0
		&& ProneMontage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference() == ProneIdleAnim.Get();
	if (bIdlePlaying) return;
	if (ProneMontage && Anim->Montage_IsPlaying(ProneMontage) && Now < ProneIdleAt + .05) return;
	if (Anim->IsAnyMontagePlaying() && !(ProneMontage && Anim->Montage_IsPlaying(ProneMontage))) return;
	if (auto* Idle = ProneIdleAnim.LoadSynchronous())
		ProneMontage = Anim->PlaySlotAnimationAsDynamicMontage(Idle, TEXT("DefaultSlot"), .25f, .2f, 1.f, 100000);
}

void ABrokenEdgeCharacter::UpdateSprint()
{
	if (!bEnableSprint || (!HasAuthority() && !IsLocallyControlled()) || !Combat) return;
	// 倒地优先于趴下：先退出趴下恢复原值，再交给倒地逻辑。
	if (ExtractionAbilities->IsDowned() && bProne)
	{
		bProne = false;
		ApplyProne(false);
	}
	if (!ExtractionAbilities->IsDowned()) ApplyProne(bProne);
	if (ExtractionAbilities->IsDowned())
	{
		if (!bWasExtractionDowned) ExtractionSavedCrouchSpeed = GetCharacterMovement()->MaxWalkSpeedCrouched;
		bWasExtractionDowned = true;
		bSprinting = false;
		GetCharacterMovement()->MaxWalkSpeed = GetDefault<UExtractionRules>()->CrawlSpeed;
		GetCharacterMovement()->MaxWalkSpeedCrouched = GetDefault<UExtractionRules>()->CrawlSpeed;
		Crouch();
		return;
	}
	if (bWasExtractionDowned)
	{
		UnCrouch();
		GetCharacterMovement()->MaxWalkSpeedCrouched = ExtractionSavedCrouchSpeed;
		bWasExtractionDowned = false;
	}
	// 激素止痛期间骨折不影响移动（FractureHinders），止痛结束后恢复减速。
	const bool bFractureHinders = ExtractionHealth->FractureHinders();
	const float FractureScale = bFractureHinders
		                            ? GetDefault<UExtractionEquipmentRules>()->FractureSpeedScale
		                            : 1.f;
	if (bExtractionFractureSpeedApplied != bFractureHinders)
	{
		if (bFractureHinders)
		{
			ExtractionFractureCrouchSpeed = GetCharacterMovement()->MaxWalkSpeedCrouched;
			GetCharacterMovement()->MaxWalkSpeedCrouched *= FractureScale;
		}
		else GetCharacterMovement()->MaxWalkSpeedCrouched = ExtractionFractureCrouchSpeed;
		bExtractionFractureSpeedApplied = bFractureHinders;
	}
	if (bExtractionGuard)
	{
		const auto* GuardAI = Cast<AExtractionGuard>(Controller);
		const bool bPursuing = GuardAI && GuardAI->State == EExtractionGuardState::Search;
		if (HasAuthority())
		{
			const uint8 Alert = GuardAI && GuardAI->Target ? 2 : bPursuing ? 1 : 0;
			if (Alert != ExtractionAlert)
			{
				ExtractionAlert = Alert;
				ForceNetUpdate();
			}
		}
		const auto* GuardRules = GetDefault<UExtractionRules>();
		const float GuardSpeed = FMath::Max(0.f, GuardRules->GuardWalkSpeed);
		GetCharacterMovement()->MaxWalkSpeed = (bPursuing
			? FMath::Max(GuardSpeed, GuardRules->GuardPursuitSpeed) : GuardSpeed) * FractureScale;
		return;
	}
	const FVector Acceleration = GetCharacterMovement()->GetCurrentAcceleration();
	// 治疗允许移动施放但要降速，且不能边治疗边冲刺；倍率来自 UExtractionRules。
	const bool bHealing = ExtractionAbilities && ExtractionAbilities->Action == EExtractionAction::Heal;
	const bool bWasSprinting = bSprinting;
	const bool bCanSprint = bSprintRequested && !bProne && !bHealing && !bFractureHinders && !bIsCrouched && !IsAiming() &&
		!bDisableGameplay
		&& (!ExtractionCombat->IsEnabled() || ExtractionCombat->CanSprintWithWeapon())
		&& !bDefeated
		&& GetCombatState() == ECombatState::ECS_Unoccuiped
		&& FVector::DotProduct(Acceleration.GetSafeNormal2D(), GetActorForwardVector()) > 0.5f;
	// 只有搜打撤模式有体力限制，其他模式照旧。
	bSprinting = ExtractionCombat->IsEnabled()
		             ? UpdateStamina(bCanSprint, GetWorld()->GetDeltaSeconds())
		             : bCanSprint;
	// 冲刺脚步声会引来近处守卫，每 0.5 秒广播一次。
	if (bSprinting && HasAuthority() && ExtractionCombat->IsEnabled() && GetWorld()->GetTimeSeconds() >= NextFootstepNoiseAt)
	{
		NextFootstepNoiseAt = GetWorld()->GetTimeSeconds() + .5;
		AExtractionGuard::HearShot(GetWorld(), GetActorLocation(), GetDefault<UExtractionRules>()->GuardFootstepRadius);
	}
	if (bWasSprinting && !bSprinting && ExtractionCombat->IsEnabled()) ExtractionCombat->OnSprintEnded();
	GetCharacterMovement()->MaxWalkSpeed = bSprinting
		                                       ? SprintSpeed
		                                       : (IsAiming() ? Combat->AimWalkSpeed : Combat->BaseWalkSpeed);
	if (bHealing)
		GetCharacterMovement()->MaxWalkSpeed *= FMath::Clamp(
			GetDefault<UExtractionRules>()->HealMoveSpeedScale, .05f, 1.f);
	GetCharacterMovement()->MaxWalkSpeed *= FractureScale;
}

bool ABrokenEdgeCharacter::UpdateStamina(bool bWantsSprint, float DeltaSeconds)
{
	const auto* Rules = GetDefault<UExtractionRules>();
	const double Now = GetWorld()->GetTimeSeconds();
	// 已经在跑就能跑到体力见底；从静止或走路起跑需要体力不低于门槛，避免见底后抖动式起跑。
	const bool bAllowed = bWantsSprint && Stamina > 0 && (bSprinting || Stamina >= Rules->StaminaSprintMin);
	if (bAllowed)
	{
		Stamina = FMath::Max(0.f, Stamina - Rules->StaminaDrainPerSecond * DeltaSeconds);
		StaminaRegenAt = Now + Rules->StaminaRegenDelay;
	}
	else if (Now >= StaminaRegenAt)
		Stamina = FMath::Min(Rules->StaminaMax, Stamina + Rules->StaminaRegenPerSecond * DeltaSeconds);
	return bAllowed;
}

float ABrokenEdgeCharacter::GetStaminaFraction() const
{
	return Stamina / FMath::Max(1.f, GetDefault<UExtractionRules>()->StaminaMax);
}

void ABrokenEdgeCharacter::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABrokenEdgeCharacter, bSprinting);
	DOREPLIFETIME_CONDITION(ABrokenEdgeCharacter, Stamina, COND_OwnerOnly);
	DOREPLIFETIME(ABrokenEdgeCharacter, bExtractionGuard);
	DOREPLIFETIME(ABrokenEdgeCharacter, ExtractionAlert);
	DOREPLIFETIME(ABrokenEdgeCharacter, bProne);
	DOREPLIFETIME(ABrokenEdgeCharacter, Zipline);
	DOREPLIFETIME(ABrokenEdgeCharacter, ZipFrom);
	DOREPLIFETIME(ABrokenEdgeCharacter, ZipTo);
	DOREPLIFETIME(ABrokenEdgeCharacter, ExtractionGuardId);

	DOREPLIFETIME_CONDITION(ABrokenEdgeCharacter, OverlappingWeapon, COND_OwnerOnly);
	// DOREPLIFETIME(ABrokenEdgeCharacter, OverlappingWeapon);
	DOREPLIFETIME(ABrokenEdgeCharacter, Health);
	DOREPLIFETIME(ABrokenEdgeCharacter, bDisableGameplay);
	DOREPLIFETIME(ABrokenEdgeCharacter, Shield);
}

void ABrokenEdgeCharacter::PostInitializeComponents()
{
	// 使用 ExtractionGuard（含 BP_EnemyAIController 子类）的角色自动按敌人处理。
	// 必须放在 Super 前：APawn::PostInitializeComponents 会在其中决定是否生成 AI Controller。
	// 这使现有 BP_EnemyCharacter 无需重新设父类，在任意地图放置或运行时生成都能被接管。
	if (HasAuthority() && GetWorld() && GetWorld()->IsGameWorld()
		&& (bExtractionGuard || (AIControllerClass && AIControllerClass->IsChildOf(AExtractionGuard::StaticClass()))))
	{
		bExtractionGuard = true;
		if (!AIControllerClass || !AIControllerClass->IsChildOf(AExtractionGuard::StaticClass()))
			AIControllerClass = AExtractionGuard::StaticClass();
		AutoPossessPlayer = EAutoReceiveInput::Disabled;
		AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	}
	Super::PostInitializeComponents();

	if (Combat)
	{
		Combat->Character = this;
	}

	if (Buff)
	{
		Buff->Character = this;
		Buff->InitialJumpZVelocity = GetCharacterMovement()->JumpZVelocity;
		Buff->InitialBaseSpeed = GetCharacterMovement()->MaxWalkSpeed;
		Buff->InitialCrouchSpeed = GetCharacterMovement()->MaxWalkSpeedCrouched;
	}

	//延迟补偿组件
	if (LagCompensation)
	{
		LagCompensation->Character = this;

		if (Controller)
		{
			LagCompensation->Controller = Cast<ABrokenEdgePlayerController>(Controller);
		}
	}
}

void ABrokenEdgeCharacter::PlaySwapMontage()
{
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (AnimInstance && GetSwapMontage())
	{
		float Rate = 1;
		if (ExtractionCombat->IsEnabled() && ExtractionCombat->GetWeaponStats().IsValid() && Combat->SecondaryWeapon)
			if (const auto* Next = WeaponMechanics::Find(Combat->SecondaryWeapon->ExtractionDefinition))
				Rate = GetSwapMontage()->GetPlayLength() / FMath::Max(
					.1f, ExtractionCombat->GetWeaponStats().Unequip + Next->Equip);
		AnimInstance->Montage_Play(GetSwapMontage(), Rate);
	}
	// 第一人称不再播旧枪自己的“拔枪+拉栓”：旧枪先放下，换到新枪后播新枪的拔枪动作。
	if (DedicatedFirstPersonMesh)
	{
		if (auto* View = DedicatedFirstPersonMesh->GetAnimInstance(); View && ViewActionMontage)
			View->Montage_Stop(.1f, ViewActionMontage);
		ViewActionMontage = nullptr;
		ViewActionThirdPersonMontage = nullptr;
		ViewSwapUnequip = .45f;
		ViewSwapEquip = .4f;
		if (ExtractionCombat->IsEnabled() && ExtractionCombat->GetWeaponStats().IsValid())
		{
			ViewSwapUnequip = ExtractionCombat->GetWeaponStats().Unequip;
			if (Combat->SecondaryWeapon)
				if (const auto* Next = WeaponMechanics::Find(Combat->SecondaryWeapon->ExtractionDefinition))
					ViewSwapEquip = Next->Equip;
		}
		ViewSwapUnequip = FMath::Max(.15f, ViewSwapUnequip);
		ViewSwapEquip = FMath::Max(.15f, ViewSwapEquip);
		const AWeapon* Next = Combat->SecondaryWeapon;
		ViewSwapDraw = Next && Next->Presentation && Next->Presentation->ViewSwap
			               ? Next->Presentation->ViewSwap.Get()
			               : FirstPersonSwapMontage.Get();
		ViewSwapFrom = Combat->EquippedWeapon;
		ViewSwapStart = GetWorld()->GetTimeSeconds();
		ViewSwapDrawStart = -1.f;
	}
}

float ABrokenEdgeCharacter::SyncFirstPersonSwap()
{
	if (ViewSwapStart < 0.f || !DedicatedFirstPersonMesh) return 0.f;
	UAnimInstance* View = DedicatedFirstPersonMesh->GetAnimInstance();
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bChanged = GetEquippedWeapon() != ViewSwapFrom.Get();
	const bool bSwapping = Combat && Combat->CombatState == ECombatState::ECS_SwappingWeapon;
	const auto End = [&]
	{
		ViewSwapStart = -1.f;
		ViewSwapDrawStart = -1.f;
		ViewSwapFrom.Reset();
		return 0.f;
	};
	// 被取消（死亡、翻越、切枪中断）或超时都直接恢复。
	if (bDefeated || IsTraversing() || Melee->Active || Now - ViewSwapStart > ViewSwapUnequip + ViewSwapEquip + 2.f
		|| (!bSwapping && !bChanged && Now - ViewSwapStart > .2f))
	{
		if (View && ViewSwapDraw && View->Montage_IsPlaying(ViewSwapDraw) && ViewSwapDrawStart < 0.f)
			View->Montage_Stop(.1f, ViewSwapDraw);
		return End();
	}
	if (!bChanged)
		return FMath::SmoothStep(0.f, 1.f, (Now - ViewSwapStart) / ViewSwapUnequip);
	if (ViewSwapDrawStart < 0.f)
	{
		ViewSwapDrawStart = Now;
		// 源拔枪动作较长（含拉栓），按装备时间加快，但不超过 2.5 倍，余下部分在可开火后自然收尾。
		if (View && ViewSwapDraw)
			View->Montage_Play(ViewSwapDraw, FMath::Clamp(
				                   ViewSwapDraw->GetPlayLength() / (ViewSwapEquip + .35f), 1.f, 2.5f));
	}
	const float Raise = (Now - ViewSwapDrawStart) / (ViewSwapDraw && View ? .12f : ViewSwapEquip);
	if (Raise >= 1.f) return End();
	return 1.f - FMath::SmoothStep(0.f, 1.f, Raise);
}

/**
 * @brief 播放受击动画
 */
void ABrokenEdgeCharacter::PlayHitReactMontage()
{
	if (bDefeated || IsTraversing() || Combat == nullptr || HitReactMontage == nullptr) return;

	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (AnimInstance)
	{
		AnimInstance->Montage_Play(HitReactMontage);
		FName SectionName("FromRight");
		AnimInstance->Montage_JumpToSection(SectionName);
	}
}

void ABrokenEdgeCharacter::PlayAbilityMontage(uint8 ActionKind, float Seconds)
{
	if (ActionKind == 4)
	{
		if (HealOtherMontage == nullptr) ActionKind = 2;
		else
		{
			StopAbilityMontage();
			AbilityMontage = HealOtherMontage;
			if (GetNetMode() == NM_DedicatedServer) return;
			if (UAnimInstance* Body = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
				Body->Montage_Play(HealOtherMontage, FMath::Clamp(
					                   HealOtherMontage->GetPlayLength() / FMath::Max(Seconds, .1f), .5f, 2.5f));
			PlayFirstPersonMontage(FirstPersonHealOtherMontage, HealOtherMontage);
			return;
		}
	}
	UAnimMontage* ThirdPerson = ActionKind == 1
		                            ? ReviveMontage.Get()
		                            : ActionKind == 2
		                            ? HealMontage.Get()
		                            : ActionKind == 3
		                            ? SmokeMontage.Get()
		                            : nullptr;
	UAnimMontage* View = ActionKind == 1
		                     ? FirstPersonReviveMontage.Get()
		                     : ActionKind == 2
		                     ? FirstPersonHealMontage.Get()
		                     : ActionKind == 3
		                     ? FirstPersonSmokeMontage.Get()
		                     : nullptr;
	if (ThirdPerson == nullptr) return;
	StopAbilityMontage();
	AbilityMontage = ThirdPerson;
	// 专用服务器没有可见网格，跳过表现层，避免无意义地评估蒙太奇。
	if (GetNetMode() == NM_DedicatedServer) return;
	UAnimInstance* Body = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (Body == nullptr) return;
	// 持续型动作（救援）按源节奏循环，短动作（治疗、烟雾）拉伸到与服务器结算同时长。
	const bool bLooping = ActionKind == 1;
	// 烟雾投掷按官方原速播放：0.433 s 出手与服务器生成投掷物同步，后半段收手动作完整播完。
	const float Rate = bLooping || ActionKind == 3
		                   ? 1.f
		                   : FMath::Clamp(ThirdPerson->GetPlayLength() / FMath::Max(Seconds, .1f), .5f, 2.5f);
	Body->Montage_Play(ThirdPerson, Rate);
	if (bLooping)
	{
		const FName Section = ThirdPerson->GetSectionName(0);
		if (!Section.IsNone()) Body->Montage_SetNextSection(Section, Section, ThirdPerson);
	}
	PlayFirstPersonMontage(View, ThirdPerson);
}

void ABrokenEdgeCharacter::StopAbilityMontage()
{
	if (AbilityMontage == nullptr) return;
	if (UAnimInstance* Body = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		if (Body->Montage_IsPlaying(AbilityMontage)) Body->Montage_Stop(.15f, AbilityMontage);
	}
	// 第一人称蒙太奇由 SyncFirstPersonAction 跟随第三人称一起停止。
	AbilityMontage = nullptr;
}

void ABrokenEdgeCharacter::DiagnoseStuckMovement(float DeltaTime)
{
#if UE_BUILD_SHIPPING
	return;
#else
	const auto* Move = GetCharacterMovement();
	if (Move == nullptr || !IsLocallyControlled() || IsTraversing())
	{
		StuckSeconds = 0.f;
		bStuckReported = false;
		return;
	}
	// 有明确移动输入、却几乎没有水平位移，持续 0.5 秒即视为卡住。
	// bDisableGameplay / 倒地 / 是否贴地都不跳过——它们本身可能就是原因或佐证，一并写进日志。
	// 注意不能读 GetPendingInputVector：移动组件在 Tick 里已经消费掉了，恒为零。
	const bool bWantsMove = Move->GetCurrentAcceleration().SizeSquared2D() > 100.f
		|| Move->GetLastInputVector().SizeSquared2D() > .01f;
	const bool bBlocked = GetVelocity().SizeSquared2D() < 100.f;
	if (!bWantsMove || !bBlocked)
	{
		StuckSeconds = 0.f;
		bStuckReported = false;
		return;
	}
	StuckSeconds += DeltaTime;
	if (StuckSeconds < .5f || bStuckReported) return;
	bStuckReported = true;
	++StuckEpisodes;
	FString Blockers;
	TArray<AActor*> Overlapping;
	GetOverlappingActors(Overlapping);
	for (const AActor* Actor : Overlapping)
	{
		if (Actor == nullptr || Actor == this) continue;
		Blockers += Actor->GetName() + TEXT(";");
	}
	UE_LOG(LogTemp, Warning,
	       TEXT("STUCK_TRACE loc=%s mode=%d custom=%d walkableFloorZ=%.1f stepHeight=%.1f speed=%.1f accel=%.1f "
		       "maxSpeed=%.0f disableGameplay=%d defeated=%d combat=%d lastInput=%s overlaps=%s"),
	       *GetActorLocation().ToCompactString(), static_cast<int32>(Move->MovementMode),
	       Move->CustomMovementMode, Move->GetWalkableFloorZ(), Move->MaxStepHeight,
	       GetVelocity().Size2D(), Move->GetCurrentAcceleration().Size2D(), Move->MaxWalkSpeed,
	       bDisableGameplay ? 1 : 0, IsDefeated() ? 1 : 0, static_cast<int32>(GetCombatState()),
	       *Move->GetLastInputVector().ToCompactString(), *Blockers);
	// 前向多高度射线：区分「撞墙」与「被矮台阶/边沿挡住」，并指出具体是哪个对象。
	const FVector Forward = Move->GetLastInputVector().GetSafeNormal2D();
	const auto* Capsule = GetCapsuleComponent();
	if (!Forward.IsNearlyZero() && Capsule != nullptr)
	{
		const float Radius = Capsule->GetScaledCapsuleRadius();
		const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		const float FeetZ = GetActorLocation().Z - HalfHeight;
		FCollisionQueryParams ProbeParams(SCENE_QUERY_STAT(StuckProbe), false, this);
		for (const float Height : {10.f, 40.f, 70.f, HalfHeight})
		{
			const FVector Start(GetActorLocation().X, GetActorLocation().Y, FeetZ + Height);
			const FVector End = Start + Forward * (Radius + 140.f);
			FHitResult Hit;
			if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, ProbeParams)) continue;
			// 静态网格特指到资源名，日志里能直接看出是哪块模型挡路。
			FString MeshName;
			// GetStaticMesh() 返回 TObjectPtr，auto* 推导不出指针类型，这里显式声明。
			if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Hit.GetComponent()))
			{
				if (const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
					MeshName = StaticMesh->GetName();
			}
			UE_LOG(LogTemp, Warning,
			       TEXT("STUCK_HIT height=%.0f actor=%s component=%s class=%s mesh=%s impactZ=%.1f normalZ=%.2f dist=%.1f"),
			       Height, *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()),
			       Hit.GetActor() ? *Hit.GetActor()->GetClass()->GetName() : TEXT("?"),
			       MeshName.IsEmpty() ? TEXT("-") : *MeshName,
			       Hit.ImpactPoint.Z, Hit.ImpactNormal.Z, Hit.Distance);
		}
		// 沿前进方向逐段下探地面：台阶是锯齿（每级抬升一格），斜坡是平滑上升，可据此判断是否被台阶挡住。
		FString Profile;
		float Previous = TNumericLimits<float>::Lowest();
		for (int32 Sample = 0; Sample <= 8; ++Sample)
		{
			const FVector Column = GetActorLocation() + Forward * (Sample * 25.f);
			FHitResult Ground;
			const bool bGround = GetWorld()->LineTraceSingleByChannel(
				Ground, Column + FVector(0, 0, 100), Column - FVector(0, 0, 300), ECC_Visibility, ProbeParams);
			if (!bGround)
			{
				Profile += FString::Printf(TEXT(" [%d]=none"), Sample * 25);
				Previous = TNumericLimits<float>::Lowest();
				continue;
			}
			if (Previous <= TNumericLimits<float>::Lowest() + 1.f)
				Profile += FString::Printf(TEXT(" [%d]=%.0f"), Sample * 25, Ground.ImpactPoint.Z);
			else
				Profile += FString::Printf(TEXT(" [%d]=%.0f(d%+.0f)"), Sample * 25, Ground.ImpactPoint.Z,
				                           Ground.ImpactPoint.Z - Previous);
			Previous = Ground.ImpactPoint.Z;
		}
		UE_LOG(LogTemp, Warning, TEXT("STUCK_PROFILE feetZ=%.1f ground%s"), FeetZ, *Profile);

		// 迈步失败的具体关卡：引擎允许迈（CanStepUp）却被几何挡住，还是引擎直接拒绝了这一步。
		const FHitResult& FloorHit = Move->CurrentFloor.HitResult;
		const float FloorHeight = FloorHit.bBlockingHit ? FloorHit.ImpactPoint.Z : FeetZ;
		UE_LOG(LogTemp, Warning,
		       TEXT("STUCK_GATE floor actor=%s normalZ=%.2f z=%.1f flatBase=%d perch=%.1f"),
		       *GetNameSafe(FloorHit.GetActor()), FloorHit.ImpactNormal.Z, FloorHeight,
		       Move->bUseFlatBaseForFloorChecks ? 1 : 0, Move->PerchRadiusThreshold);

		// 用 ECC_Pawn（角色真正碰撞的通道）找正前方的阻挡，并用真实胶囊做一次"抬升+前扫"。
		FCollisionQueryParams PawnParams(SCENE_QUERY_STAT(StuckGate), false, this);
		const FVector ProbeFrom = FVector(GetActorLocation().X, GetActorLocation().Y, FeetZ + 5.f);
		FHitResult Wall;
		if (GetWorld()->LineTraceSingleByChannel(Wall, ProbeFrom, ProbeFrom + Forward * (Radius + 40.f),
		                                         ECC_Pawn, PawnParams))
		{
			const UPrimitiveComponent* WallComponent = Wall.GetComponent();
			UE_LOG(LogTemp, Warning,
			       TEXT("STUCK_GATE blocker actor=%s comp=%s isISM=%d profile=%s impactAboveFloor=%.1f normalZ=%.2f "
				       "canStepUpOn=%d"),
			       *GetNameSafe(Wall.GetActor()), *GetNameSafe(WallComponent),
			       WallComponent && WallComponent->IsA<UInstancedStaticMeshComponent>() ? 1 : 0,
			       WallComponent ? *WallComponent->GetCollisionProfileName().ToString() : TEXT("?"),
			       Wall.ImpactPoint.Z - FloorHeight, Wall.ImpactNormal.Z,
			       WallComponent ? (WallComponent->CanCharacterStepUp(this) ? 1 : 0) : -1);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("STUCK_GATE blocker none on ECC_Pawn within %.0f"), Radius + 40.f);
		}

		const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
		const FVector Raised = GetActorLocation() + FVector(0, 0, Move->MaxStepHeight);
		FHitResult ForwardSweep;
		const bool bRaisedBlocked = GetWorld()->SweepSingleByChannel(
			ForwardSweep, Raised, Raised + Forward * 40.f, FQuat::Identity, ECC_Pawn, CapsuleShape, PawnParams);
		FHitResult NewFloor;
		const bool bNewFloor = GetWorld()->LineTraceSingleByChannel(
			NewFloor, Raised, Raised - FVector(0, 0, HalfHeight + 60.f), ECC_Pawn, PawnParams);
		UE_LOG(LogTemp, Warning,
		       TEXT("STUCK_GATE raisedBy=%.0f sweepBlocked=%d sweepMesh=%s newFloorZ=%.1f newFloorNormalZ=%.2f "
			       "walkableZ=%.2f newFloorWalkable=%d"),
		       Move->MaxStepHeight, bRaisedBlocked ? 1 : 0,
		       bRaisedBlocked ? *GetNameSafe(ForwardSweep.GetActor()) : TEXT("-"),
		       bNewFloor ? NewFloor.ImpactPoint.Z : -99999.f, bNewFloor ? NewFloor.ImpactNormal.Z : -1.f,
		       Move->GetWalkableFloorZ(),
		       bNewFloor && Move->IsWalkable(NewFloor) ? 1 : 0);
	}
#endif
}

void ABrokenEdgeCharacter::PlayReloadMontage()
{
	if (Combat == nullptr || Combat->EquippedWeapon == nullptr || GetReloadMontage() == nullptr) return;

	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (AnimInstance == nullptr)return;

	if (auto* Reload = ProneAnim(ProneReloadAnim))
	{
		// 趴着换弹：身体播趴姿换弹并按武器换弹时间缩放，第一人称照常。
		float Rate = 1.f;
		if (ExtractionCombat->IsEnabled() && ExtractionCombat->GetWeaponStats().IsValid())
		{
			const auto& Stats = ExtractionCombat->GetWeaponStats();
			Rate = Reload->GetPlayLength() / FMath::Max(.1f, Combat->EquippedWeapon->IsEmpty() ? Stats.EmptyReload : Stats.Reload);
		}
		AnimInstance->PlaySlotAnimationAsDynamicMontage(Reload, TEXT("DefaultSlot"), .1f, .2f, Rate, 1);
		PlayFirstPersonMontage(GetViewReloadMontage(), GetReloadMontage(), FName("Rifle"));
		return;
	}

	AnimInstance->Montage_Play(GetReloadMontage());

	FName SectionName;

	switch (Combat->EquippedWeapon->GetWeaponType())
	{
	case EWeaponType::EWT_AssaultRifle:
	case EWeaponType::EWT_RocketLauncher:
	case EWeaponType::EWT_Pistol:
	case EWeaponType::EWT_SMG:
	case EWeaponType::EWT_SniperRifle:
	case EWeaponType::EWT_GrenadeLauncher:
		SectionName = FName("Rifle");
		break;
	case EWeaponType::EWT_Shotgun:
		SectionName = FName("Shotgun");
		break;
		// case EWeaponType::EWT_Pistol:
		// 	SectionName = FName("Pistol");
		// 	break;
		// case EWeaponType::EWT_SMG:
		// 	SectionName = FName("Pistol");
		// 	break;
		// case EWeaponType::EWT_Shotgun:
		// 	SectionName = FName("Shotgun");
		// 	break;
		// case EWeaponType::EWT_SniperRifle:
		// 	SectionName = FName("SniperRifle");
		// 	break;
		// case EWeaponType::EWT_GrenadeLauncher:
		// 	SectionName = FName("GrenadeLauncher");
		// 	break;
	}

	AnimInstance->Montage_JumpToSection(SectionName);
	if (ExtractionCombat->IsEnabled() && ExtractionCombat->GetWeaponStats().IsValid())
	{
		const auto& Stats = ExtractionCombat->GetWeaponStats();
		const int32 Section = GetReloadMontage()->GetSectionIndex(SectionName);
		const float Length = Section != INDEX_NONE
			                     ? GetReloadMontage()->GetSectionLength(Section)
			                     : GetReloadMontage()->GetPlayLength();
		AnimInstance->Montage_SetPlayRate(GetReloadMontage(),
		                                  Length / FMath::Max(
			                                  .1f, Combat->EquippedWeapon->IsEmpty()
				                                       ? Stats.EmptyReload
				                                       : Stats.Reload));
	}

	PlayFirstPersonMontage(GetViewReloadMontage(), GetReloadMontage(), SectionName);
}

void ABrokenEdgeCharacter::PlayFireMontage(bool bAiming)
{
	if (auto* Fire = ProneAnim(ProneFireAnim))
	{
		// 趴着开火：身体播趴姿开火，第一人称照常播武器开火。
		if (auto* Anim = GetMesh()->GetAnimInstance())
			Anim->PlaySlotAnimationAsDynamicMontage(Fire, TEXT("DefaultSlot"), .05f, .1f, 1.f, 1);
		const auto* Profile = Combat && Combat->EquippedWeapon ? Combat->EquippedWeapon->Presentation.Get() : nullptr;
		PlayFirstPersonMontage(Profile && Profile->ViewFire ? Profile->ViewFire.Get() : FirstPersonFireMontage.Get(),
		                       Profile && Profile->BodyFire ? Profile->BodyFire.Get() : static_cast<UAnimMontage*>(FireWeaponMontage));
		return;
	}
	if (Combat && Combat->EquippedWeapon && Combat->EquippedWeapon->Presentation)
	{
		const auto* Profile = Combat->EquippedWeapon->Presentation.Get();
		if (Profile->BodyFire)
		{
			if (auto* Anim = GetMesh()->GetAnimInstance())
			{
				Anim->Montage_Play(Profile->BodyFire);
				PlayFirstPersonMontage(Profile->ViewFire, Profile->BodyFire);
			}
			return;
		}
	}
	if (Combat == nullptr || Combat->EquippedWeapon == nullptr || FireWeaponMontage == nullptr) return;

	if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
	{
		AnimInstance->Montage_Play(FireWeaponMontage);
		FName SectionName = bAiming ? FName("RifleAim") : FName("RifleHip");
		// UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
		//        TEXT(""));
		AnimInstance->Montage_JumpToSection(SectionName);
		const auto* Profile = Combat->EquippedWeapon->Presentation.Get();
		PlayFirstPersonMontage(Profile && Profile->ViewFire ? Profile->ViewFire.Get() : FirstPersonFireMontage.Get(),
		                       FireWeaponMontage, SectionName);
	}
}

void ABrokenEdgeCharacter::PlayDefeatMontage()
{
	if (DefeatedMontage == nullptr)return;
	if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
	{
		AnimInstance->Montage_Play(DefeatedMontage);
	}
}

void ABrokenEdgeCharacter::UpdateHUDHealth()
{
	// UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, Controller?*Controller->GetName():TEXT("空"));
	BrokenEdgePlayerController = BrokenEdgePlayerController == nullptr
		                             ? Cast<ABrokenEdgePlayerController>(GetController())
		                             : BrokenEdgePlayerController;

	if (BrokenEdgePlayerController)
	{
		BrokenEdgePlayerController->SetHUDHealth(Health, MaxHealth);
	}
}


/**
 * @brief 更新HUD护盾显示
 */
void ABrokenEdgeCharacter::UpdateHUDShield()
{
	BrokenEdgePlayerController = BrokenEdgePlayerController == nullptr
		                             ? Cast<ABrokenEdgePlayerController>(Controller)
		                             : BrokenEdgePlayerController;

	if (BrokenEdgePlayerController)
	{
		BrokenEdgePlayerController->SetHUDShield(Shield, MaxShield);
	}
}

/**
 * @brief 更新HUD弹药显示
 */
void ABrokenEdgeCharacter::UpdateHUDAmmo()
{
	BrokenEdgePlayerController = BrokenEdgePlayerController == nullptr
		                             ? Cast<ABrokenEdgePlayerController>(Controller)
		                             : BrokenEdgePlayerController;

	if (BrokenEdgePlayerController && Combat && Combat->EquippedWeapon)
	{
		BrokenEdgePlayerController->SetHUDCarriedAmmo(Combat->CarriedAmmo);
		BrokenEdgePlayerController->SetHUDWeaponAmmo(Combat->EquippedWeapon->GetAmmo());
	}
}

/**
 * @brief 跳跃处理
 */
bool ABrokenEdgeCharacter::IsTraversing() const
{
	return Traversal && Traversal->Active();
}

void ABrokenEdgeCharacter::Jump()
{
	if (IsTraversing())
	{
		auto* Movement = CastChecked<UTraversalMovementComponent>(GetCharacterMovement());
		if (Movement->OnLadder()) Movement->RequestDetach(true);
		return;
	}
	if (ExtractionAbilities->IsDowned()) return;
	// 滑索上按空格松手：本地立即落下，服务器随后确认。
	if (Zipline)
	{
		ApplyZipline(false);
		ServerEndZipline();
		return;
	}
	if (GetCharacterMovement()->IsSwimming())
	{
		// 按住空格上浮；同时走一次跳跃输入，靠岸时由翻越组件判定能否攀上岸（不会在水里起跳）。
		bSwimUp = true;
		Super::Jump();
		return;
	}
	// 趴着时空格先起身，不直接起跳。
	if (bProne)
	{
		ToggleProne();
		return;
	}
	if (ExtractionInteraction->IsEnabled())
		if (auto* PC = Cast<AExtractionPlayerController>(Controller)) PC->ServerCancelMedical();
	if (Combat && Combat->bHoldingTheFlag) return;
	if (Combat == nullptr) return;
	if (bDisableGameplay) return;
	if (bIsCrouched)
	{
		UnCrouch();
	}
	else
	{
		Super::Jump();
	}
}

void ABrokenEdgeCharacter::StopJumping()
{
	bSwimUp = false;
	Super::StopJumping();
}

void ABrokenEdgeCharacter::Move(const FInputActionValue& Value)
{
	if (bDisableGameplay) return;
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();
	// 滑索上 WASD 不起作用，移动输入由 TickZipline 沿索方向给出。
	if (Zipline) return;
	if (IsTraversing())
	{
		const auto* Movement = CastChecked<UTraversalMovementComponent>(GetCharacterMovement());
		if (Movement->OnLadder()) AddMovementInput(Movement->State.Facing, MovementVector.Y);
		return;
	}

	if (Controller != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector；游泳时沿视线方向前进，低头下潜、抬头上浮。
		const bool bSwimming = GetCharacterMovement()->IsSwimming();
		const FVector ForwardDirection = bSwimming ? Rotation.Vector()
			: FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement
		AddMovementInput(ForwardDirection, MovementVector.Y);
		AddMovementInput(RightDirection, MovementVector.X);
	}
}

void ABrokenEdgeCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}

void ABrokenEdgeCharacter::CrouchButtonPressed()
{
	if (IsTraversing()) return;
	if (Combat && Combat->bHoldingTheFlag) return;
	if (bDisableGameplay) return;
	if (Combat == nullptr) return;
	// 趴着时按蹲键回到站立。
	if (bProne)
	{
		ToggleProne();
		return;
	}

	if (bIsCrouched)
	{
		UnCrouch();
	}
	else
	{
		Crouch();
	}
}

void ABrokenEdgeCharacter::InteractDoorPressed()
{
	// 撤离模式里 F 只负责开门：卡与耐久都由服务器校验。
	if (!ExtractionInteraction->IsEnabled()) return;
	if (auto* PC = Cast<AExtractionPlayerController>(Controller)) PC->InteractDoor();
}

void ABrokenEdgeCharacter::EquipButtonPressed()
{
	if (ExtractionInteraction->IsEnabled())
	{
		if (auto* PC = Cast<AExtractionPlayerController>(Controller)) PC->Interact();
		return;
	}
	if (bDisableGameplay) return;
	if (Combat == nullptr) return;

	if (Combat->bHoldingTheFlag) return;

	if (Combat->CombatState == ECombatState::ECS_Unoccuiped) ServerEquipButtonPressed();

	bool bSwap = Combat->ShouldSwapWeapons() &&
		!HasAuthority() &&
		Combat->CombatState == ECombatState::ECS_Unoccuiped &&
		OverlappingWeapon == nullptr;

	if (bSwap)
	{
		PlaySwapMontage();
		Combat->CombatState = ECombatState::ECS_SwappingWeapon;
		bFinishedSwapping = false;
	}
}

void ABrokenEdgeCharacter::AimButtonPressed()
{
	if (Melee->Active)
	{
		Melee->Attack(true);
		return;
	}
	if (ExtractionInteraction->IsEnabled() && ExtractionInteraction->InventoryOpen) return;
	if (Combat && Combat->bHoldingTheFlag) return;
	if (bDisableGameplay) return;
	if (Combat)
	{
		Combat->SetAiming(true);
	}
}

void ABrokenEdgeCharacter::AimButtonReleased()
{
	if (Combat && Combat->bHoldingTheFlag) return;
	if (bDisableGameplay) return;
	if (Combat)
	{
		Combat->SetAiming(false);
	}
}

/**
 * @brief 换弹按钮按下处理
 */
void ABrokenEdgeCharacter::ReloadButtonPressed()
{
	if (Melee->Active) return;
	if (ExtractionAbilities->bStimDrawn) return;
	if (Combat && Combat->bHoldingTheFlag) return;
	if (bDisableGameplay) return;
	if (Combat)
	{
		Combat->Reload();
	}
}

void ABrokenEdgeCharacter::DropButtonPressed()
{
	// if (Combat && Combat->bHoldingTheFlag) return;

	if (bDisableGameplay) return;
	if (Combat)
	{
		Combat->Drop();
	}
}

/**
 * @brief 开火按钮按下处理
 */
void ABrokenEdgeCharacter::FireButtonPressed()
{
	if (Melee->Active)
	{
		Melee->Attack(false);
		return;
	}
	if (ExtractionInteraction->IsEnabled() && ExtractionInteraction->InventoryOpen) return;
	if (Combat && Combat->bHoldingTheFlag) return;
	if (bDisableGameplay) return;
	// 手上是激素枪：左键射飞镖，不开枪。
	if (ExtractionAbilities->bStimDrawn)
	{
		ExtractionAbilities->FireStimDart();
		return;
	}
	if (Combat)
	{
		Combat->FireButtonPressed(true);
	}
}

/**
 * @brief 开火按钮松开处理
 */
void ABrokenEdgeCharacter::FireButtonReleased()
{
	if (Combat && Combat->bHoldingTheFlag) return;
	if (bDisableGameplay) return;
	if (Combat)
	{
		Combat->FireButtonPressed(false);
	}
}


void ABrokenEdgeCharacter::OnRep_OverlappingWeapon(AWeapon* LastWeapon)
{
	if (OverlappingWeapon)
	{
		OverlappingWeapon->ShowPickupWidget(true);
	}
	if (LastWeapon)
	{
		LastWeapon->ShowPickupWidget(false);
	}
}

/**
 * @brief 服务器端装备按钮按下（RPC实现）
 */
void ABrokenEdgeCharacter::ServerEquipButtonPressed_Implementation()
{
	if (ExtractionInteraction->IsEnabled())
	{
		if (ExtractionCombat->CanAct() && !ExtractionInteraction->InventoryOpen && Combat->ShouldSwapWeapons())
		{
			ExtractionInteraction->CancelMedical();
			Combat->SwapWeapons();
		}
		return;
	}
	if (Combat)
	{
		if (OverlappingWeapon)
		{
			Combat->EquipWeapon(OverlappingWeapon);
		}
		else if (Combat->ShouldSwapWeapons())
		{
			Combat->SwapWeapons();
		}
	}
}


void ABrokenEdgeCharacter::OnRep_Shield(float LastShield)
{
	UpdateHUDShield();
	if (Shield < LastShield)
	{
		PlayHitReactMontage();
	}
}

void ABrokenEdgeCharacter::OnRep_Health(float LastHealth)
{
	UpdateHUDHealth();

	if (Health < LastHealth)
	{
		PlayHitReactMontage();
	}
}

void ABrokenEdgeCharacter::PollInit()
{
	//@TODO


	// if (BrokenEdgePlayerState == nullptr)
	// {
	// 	BlasterPlayerState = GetPlayerState<ABlasterPlayerState>();
	// 	if (BlasterPlayerState)
	// 	{
	// 		OnPlayerStateInitialized();
	//
	// 		ABlasterGameState* BlasterGameState = Cast<ABlasterGameState>(UGameplayStatics::GetGameState(this));
	// 		if (BlasterGameState && BlasterGameState->TopScoringPlayers.Contains(BlasterPlayerState))
	// 		{
	// 			MulticastGainedTheLead();
	// 		}
	// 	}
	// }

	if (BrokenEdgePlayerController == nullptr)
	{
		BrokenEdgePlayerController = Cast<ABrokenEdgePlayerController>(Controller);
		if (BrokenEdgePlayerController)
		{
			UpdateHUDAmmo();
			UpdateHUDHealth();
			UpdateHUDShield();
		}
	}
}

void ABrokenEdgeCharacter::SetOverlappingWeapon(AWeapon* Weapon)
{
	if (OverlappingWeapon)
	{
		OverlappingWeapon->ShowPickupWidget(false);
	}

	OverlappingWeapon = Weapon;

	if (IsLocallyControlled())
	{
		if (OverlappingWeapon)
		{
			OverlappingWeapon->ShowPickupWidget(true);
		}
	}
}

bool ABrokenEdgeCharacter::IsWeaponEquipped()
{
	return (Combat && Combat->EquippedWeapon);
}

/**
 * @brief 是否正在瞄准
 * @return 是否瞄准
 */
bool ABrokenEdgeCharacter::IsAiming()
{
	return (Combat && Combat->bAiming);
}


/**
 * @brief 获取当前装备的武器
 * @return 当前装备的武器
 */
AWeapon* ABrokenEdgeCharacter::GetEquippedWeapon()
{
	if (Combat == nullptr) return nullptr;
	return Combat->EquippedWeapon;
}

/**
 * @brief 获取命中目标位置
 * @return 命中目标位置
 */
FVector ABrokenEdgeCharacter::GetHitTarget() const
{
	if (bExtractionGuard)
	{
		// AI 没有玩家准星射线，Combat.HitTarget 通常仍为零。动画手部应跟随战斗关注点。
		if (const auto* AI = Cast<AAIController>(GetController()))
		{
			const FVector Focus = AI->GetFocalPointForPriority(EAIFocusPriority::Gameplay);
			if (FAISystem::IsValidLocation(Focus)) return Focus;
		}
		return GetPawnViewLocation() + GetActorForwardVector() * 10000.f;
	}
	if (Combat == nullptr) return FVector();
	return Combat->HitTarget;
}

/**
 * @brief 获取战斗状态
 * @return 战斗状态
 */
ECombatState ABrokenEdgeCharacter::GetCombatState() const
{
	if (Combat == nullptr) return ECombatState::ECS_MAX;
	return Combat->CombatState;
}

/**
* @brief 是否正在本地换弹
* @return 是否换弹中
*/
bool ABrokenEdgeCharacter::IsLocallyReloading()
{
	if (Combat == nullptr) return false;
	return Combat->bLocallyReloading;
}

void ABrokenEdgeCharacter::OnRep_ReplicatedMovement()
{
	Super::OnRep_ReplicatedMovement();
	SimProxiesTurn();

	TimeSinceLastMovementReplication = 0.f;
}

void ABrokenEdgeCharacter::SimProxiesTurn()
{
	if (Combat == nullptr || Combat->EquippedWeapon == nullptr) return;
	bRotateRootBone = false;

	float Speed = CalculateSpeed();
	if (Speed > 0.f)
	{
		TurningInPlace = ETurningInPlace::ETIP_NotTurning;
		return;
	}

	ProxyRotationLastFrame = ProxyRotation;
	ProxyRotation = GetActorRotation();
	ProxyYaw = UKismetMathLibrary::NormalizedDeltaRotator(ProxyRotation, ProxyRotationLastFrame).Yaw;

	if (FMath::Abs(ProxyYaw) > TurnThreshold)
	{
		if (ProxyYaw > TurnThreshold)
		{
			TurningInPlace = ETurningInPlace::ETIP_Right;
		}
		else if (ProxyYaw < -TurnThreshold)
		{
			TurningInPlace = ETurningInPlace::ETIP_Left;
		}
		else
		{
			TurningInPlace = ETurningInPlace::ETIP_NotTurning;
		}
		return;
	}
	TurningInPlace = ETurningInPlace::ETIP_NotTurning;
}

void ABrokenEdgeCharacter::Defeated(bool bPlayerLeftGame)
{
	if (bDefeated) return;
	// 死亡时立即终止行为树及延迟启动，避免尸体继续执行关注、移动或其他任务。
	if (HasAuthority())
		if (auto* Guard = Cast<AExtractionGuard>(GetController())) Guard->StopGuardAI();
	if (ExtractionCombat->IsEnabled()) ExtractionCombat->CancelReload();
	DropWeapons();
	MulticastDefeated(bPlayerLeftGame);
}

void ABrokenEdgeCharacter::MulticastDefeated_Implementation(bool bPlayerLeftGame)
{
	ReleaseFirstPersonView();
	bLeftGame = bPlayerLeftGame;

	if (BrokenEdgePlayerController)
	{
		BrokenEdgePlayerController->SetHUDWeaponAmmo(0);
	}
	bDefeated = true;

	PlayDefeatMontage();

	// 禁用角色移动
	bDisableGameplay = true;
	if (IsTraversing()) CastChecked<UTraversalMovementComponent>(GetCharacterMovement())->Finish();
	GetCharacterMovement()->DisableMovement();

	if (Combat)
	{
		Combat->FireButtonPressed(false);
	}

	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 设置淘汰计时器，触发重生或离开游戏
	GetWorldTimerManager().SetTimer(
		RespawnTimer,
		this,
		&ABrokenEdgeCharacter::RespawnTimerFinished,
		RespawnDelay
	);
}

void ABrokenEdgeCharacter::RespawnTimerFinished()
{
	BrokenEdgeGameMode = BrokenEdgeGameMode == nullptr
		                     ? GetWorld()->GetAuthGameMode<ABrokenEdgeGameMode>()
		                     : BrokenEdgeGameMode;

	if (BrokenEdgeGameMode && !bLeftGame)
	{
		BrokenEdgeGameMode->RequestRespawn(this, Controller);
	}
	// if (bLeftGame && IsLocallyControlled())
	// {
	// 	OnLeftGame.Broadcast();
	// }
}

void ABrokenEdgeCharacter::DropWeapons()
{
	if (ExtractionInteraction->IsEnabled())
	{
		ExtractionInteraction->DropOnExit();
		return;
	}
	if (Combat == nullptr) return;

	if (Combat->EquippedWeapon)
	{
		Combat->EquippedWeapon->Dropped();
		Combat->EquippedWeapon = nullptr;
	}
	if (Combat->SecondaryWeapon)
	{
		Combat->SecondaryWeapon->Dropped();
		Combat->SecondaryWeapon = nullptr;
	}
}

const FStingerViewWeaponFit& ABrokenEdgeCharacter::GetViewWeaponFit() const
{
	const auto* Weapon = Combat ? Combat->EquippedWeapon : nullptr;
	return Weapon && Weapon->Presentation ? Weapon->Presentation->ViewFit : FirstPersonWeaponFit;
}

UAnimMontage* ABrokenEdgeCharacter::GetReloadMontage() const
{
	const auto* Weapon = Combat ? Combat->EquippedWeapon : nullptr;
	return Weapon && Weapon->Presentation && Weapon->Presentation->BodyReload
		       ? Weapon->Presentation->BodyReload.Get()
		       : ReloadMontage;
}

UAnimMontage* ABrokenEdgeCharacter::GetSwapMontage() const
{
	const auto* Weapon = Combat ? Combat->EquippedWeapon : nullptr;
	return Weapon && Weapon->Presentation && Weapon->Presentation->BodySwap
		       ? Weapon->Presentation->BodySwap.Get()
		       : SwapMontage;
}

UAnimMontage* ABrokenEdgeCharacter::GetViewReloadMontage() const
{
	const auto* Weapon = Combat ? Combat->EquippedWeapon : nullptr;
	return Weapon && Weapon->Presentation && Weapon->Presentation->ViewReload
		       ? Weapon->Presentation->ViewReload.Get()
		       : FirstPersonReloadMontage.Get();
}

UAnimMontage* ABrokenEdgeCharacter::GetViewSwapMontage() const
{
	const auto* Weapon = Combat ? Combat->EquippedWeapon : nullptr;
	return Weapon && Weapon->Presentation && Weapon->Presentation->ViewSwap
		       ? Weapon->Presentation->ViewSwap.Get()
		       : FirstPersonSwapMontage.Get();
}

void ABrokenEdgeCharacter::StartZipline(AExtractionZipline* Line, const FVector& From, const FVector& To)
{
	if (!HasAuthority() || !IsValid(Line)) return;
	if (bIsCrouched) UnCrouch();
	Zipline = Line;
	ZipFrom = From;
	ZipTo = To;
	ZipStartedAt = GetWorld()->GetTimeSeconds();
	OnRep_Zipline();
	ForceNetUpdate();
}

void ABrokenEdgeCharacter::EndZipline()
{
	if (!HasAuthority() || !Zipline) return;
	Zipline = nullptr;
	OnRep_Zipline();
	ForceNetUpdate();
}

void ABrokenEdgeCharacter::ServerEndZipline_Implementation() { EndZipline(); }

void ABrokenEdgeCharacter::OnRep_Zipline() { ApplyZipline(Zipline != nullptr); }

void ABrokenEdgeCharacter::ApplyZipline(bool bOn)
{
	if (bOn == bZipApplied || !(HasAuthority() || IsLocallyControlled())) return;
	bZipApplied = bOn;
	auto* Movement = GetCharacterMovement();
	if (bOn)
	{
		ZipSavedFlySpeed = Movement->MaxFlySpeed;
		Movement->MaxFlySpeed = 1100.f;
		Movement->Velocity = FVector::ZeroVector;
		Movement->SetMovementMode(MOVE_Flying);
		return;
	}
	Movement->MaxFlySpeed = ZipSavedFlySpeed > 0 ? ZipSavedFlySpeed : Movement->MaxFlySpeed;
	// 落点在水里（池子注了水）就直接游泳，否则正常下落。
	const auto* Volume = Movement->GetPhysicsVolume();
	Movement->SetMovementMode(Volume && Volume->bWaterVolume ? MOVE_Swimming : MOVE_Falling);
}

bool ABrokenEdgeCharacter::SweepBody(const FVector& Dir, float TopHeight, float Distance, FHitResult& Hit) const
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const float Feet = GetActorLocation().Z - Capsule->GetScaledCapsuleHalfHeight();
	constexpr float Lift = 20.f; // 抬离地面，脚下的地面、小台阶不算挡
	const float Half = FMath::Max((TopHeight - Lift) * .5f, 1.f);
	const float Radius = FMath::Min(Capsule->GetScaledCapsuleRadius(), Half);
	const FVector Start(GetActorLocation().X, GetActorLocation().Y, Feet + Lift + Half);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AutoCrouch), false, this);
	FCollisionResponseParams Response;
	Capsule->InitSweepCollisionParams(Params, Response);
	return GetWorld()->SweepSingleByChannel(Hit, Start, Start + Dir * Distance, FQuat::Identity,
	                                        Capsule->GetCollisionObjectType(), FCollisionShape::MakeCapsule(Radius, Half),
	                                        Params, Response);
}

void ABrokenEdgeCharacter::TickAutoCrouch()
{
	auto* Move = GetCharacterMovement();
	if (!Move || !IsLocallyControlled()) return;
	const float StandTop = GetDefaultHalfHeight() * 2.f;
	const float CrouchTop = Move->GetCrouchedHalfHeight() * 2.f;
	constexpr float Probe = 55.f;
	const FVector Dir = Move->GetCurrentAcceleration().GetSafeNormal2D();
	if (bAutoCrouched)
	{
		// 玩家自己按了站起/趴下，或进入攀爬、滑索等状态：不再管。
		if (!bIsCrouched || bProne || IsTraversing() || Zipline)
		{
			bAutoCrouched = false;
			return;
		}
		// 前方（或停下时）不再有挡头的东西就请求站起；头顶还被挡时引擎会保持蹲姿，出来后自己站起。
		FHitResult Hit;
		if (Dir.IsNearlyZero() || !SweepBody(Dir, StandTop, Probe, Hit))
		{
			UnCrouch();
			bAutoCrouched = false;
		}
		return;
	}
	if (bIsCrouched || bProne || Dir.IsNearlyZero() || !Move->IsMovingOnGround() || IsTraversing() || Zipline
		|| bDisableGameplay)
		return;
	FHitResult Stand;
	if (!SweepBody(Dir, StandTop, Probe, Stand)) return;
	// 挡住站姿的是头顶上方的东西（命中点高于蹲姿头顶），并且蹲着能往前走得更远，才算低门楣。
	const float Feet = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (Stand.bStartPenetrating || Stand.ImpactPoint.Z < Feet + CrouchTop - 5.f) return;
	FHitResult Low;
	if (SweepBody(Dir, CrouchTop, Probe, Low) && Low.Distance < Stand.Distance + 20.f) return;
	Crouch();
	bAutoCrouched = true;
}

void ABrokenEdgeCharacter::TickZipline()
{
	if (!Zipline) return;
	const FVector At = GetActorLocation();
	const FVector Span = ZipTo - ZipFrom;
	const float Length = Span.Size();
	const FVector Dir = Span.GetSafeNormal();
	const bool bArrived = FVector::Dist(At, ZipTo) < 110.f;
	// 本地操控端：朝索上前方 2.5 米的点加输入，既沿索前进又把偏离拉回索下。
	if (IsLocallyControlled() && bZipApplied)
	{
		if (bArrived)
		{
			ApplyZipline(false);
			if (!HasAuthority()) ServerEndZipline();
		}
		else
		{
			const float Along = FMath::Clamp(FVector::DotProduct(At - ZipFrom, Dir), 0.f, Length);
			const FVector Aim = ZipFrom + Dir * FMath::Min(Length, Along + 250.f);
			AddMovementInput((Aim - At).GetSafeNormal(), 1.f);
		}
	}
	// 服务器兜底：到端点、卡住超时或滑索失效都结束。
	if (HasAuthority() && (bArrived || !IsValid(Zipline) || ExtractionAbilities->IsDowned()
		|| GetWorld()->GetTimeSeconds() - ZipStartedAt > Length / 300.f + 4.f))
		EndZipline();
}
