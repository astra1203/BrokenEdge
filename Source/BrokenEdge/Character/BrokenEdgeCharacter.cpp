// Fill out your copyright notice in the Description page of Project Settings.


#include "BrokenEdgeCharacter.h"
#include "StingerFirstPersonAnimInstance.h"
#include "Animation/AnimMontage.h"

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
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Net/UnrealNetwork.h"


// Sets default values
ABrokenEdgeCharacter::ABrokenEdgeCharacter()
{
	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetMesh());
	CameraBoom->TargetArmLength = 600.f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;

	OverheadWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("OverheadWidget"));
	OverheadWidget->SetupAttachment(RootComponent);

	Combat = CreateDefaultSubobject<UCombatComponent>(TEXT("CombatComponent"));
	Combat->SetIsReplicated(true);

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

	float DamageToHealth = Damage;
	if (Shield > 0.f)
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
		BrokenEdgePlayerController = BrokenEdgePlayerController == nullptr
			                             ? Cast<ABrokenEdgePlayerController>(Controller)
			                             : BrokenEdgePlayerController;

		ABrokenEdgePlayerController* AttackerController = Cast<ABrokenEdgePlayerController>(InstigatorController);

		BrokenEdgeGameMode->PlayerDefeated(this,
		                                   BrokenEdgePlayerController, AttackerController);
	}
}

// Called when the game starts or when spawned
void ABrokenEdgeCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		OnTakeAnyDamage.AddDynamic(this, &ABrokenEdgeCharacter::ReceiveDamage);
	}
}

void ABrokenEdgeCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetFirstPersonView(false);
	RefreshFirstPersonWorldWeapons();
	FirstPersonWorldWeapons.Reset();
	Super::EndPlay(EndPlayReason);
}

// Called every frame
void ABrokenEdgeCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateSprint();
	if (bStartInFirstPerson && IsLocallyControlled() && !bFirstPersonView && !bDefeated && !bDisableGameplay)
	{
		// 只在首次获得控制权时应用默认视角，之后允许自由切换。
		bStartInFirstPerson = false;
		SetFirstPersonView(true);
	}
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
void ABrokenEdgeCharacter::RotateInPlace(float DeltaTime)
{
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
	if (bFirstPersonView && !IsLocallyControlled())
	{
		SetFirstPersonView(false);
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
		if (ensureMsgf(ToggleCameraViewAction, TEXT("Assign ToggleCameraViewAction in the character Blueprint.")))
		{
			EnhancedInputComponent->BindAction(ToggleCameraViewAction, ETriggerEvent::Started,
			                                   this, &ABrokenEdgeCharacter::ToggleCameraView);
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

void ABrokenEdgeCharacter::ToggleCameraView()
{
	if (!IsLocallyControlled() || bDisableGameplay || bDefeated) return;
	SetFirstPersonView(!bFirstPersonView);
}

void ABrokenEdgeCharacter::SetFirstPersonView(bool bEnabled)
{
	if (bFirstPersonView == bEnabled || !FollowCamera || !CameraBoom || !GetMesh()) return;

	if (bEnabled)
	{
		if (!IsLocallyControlled() || !CreateFirstPersonBody()) return;

		// Cache the Blueprint's actual camera settings before changing the attachment.
		ThirdPersonCameraTransform = FollowCamera->GetRelativeTransform();
		bThirdPersonCameraUsesControlRotation = FollowCamera->bUsePawnControlRotation;
		bOriginalMeshOwnerNoSee = GetMesh()->bOwnerNoSee;
		OriginalBodyPrimitiveType = GetMesh()->FirstPersonPrimitiveType;
		bOriginalMeshCastHiddenShadow = GetMesh()->bCastHiddenShadow;
		OriginalMeshAnimTickOption = GetMesh()->VisibilityBasedAnimTickOption;

		FollowCamera->AttachToComponent(GetCapsuleComponent(),
		                                FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		FollowCamera->bUsePawnControlRotation = true;
		UpdateFirstPersonCameraLocation();
		// The owner sees the headless display body; the original keeps full collision and shadows.
		GetMesh()->SetOwnerNoSee(true);
		GetMesh()->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
		GetMesh()->SetCastHiddenShadow(true);
		// Keep evaluating the original animation, including weapon sockets and reload notifies.
		GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		UpdateFirstPersonWeapon();
	}
	else
	{
		FirstPersonSourceWeapon = nullptr;
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
		FollowCamera->AttachToComponent(CameraBoom, FAttachmentTransformRules::KeepRelativeTransform,
		                                USpringArmComponent::SocketName);
		FollowCamera->SetRelativeTransform(ThirdPersonCameraTransform);
		FollowCamera->bUsePawnControlRotation = bThirdPersonCameraUsesControlRotation;
		GetMesh()->SetOwnerNoSee(bOriginalMeshOwnerNoSee);
		GetMesh()->SetFirstPersonPrimitiveType(OriginalBodyPrimitiveType);
		GetMesh()->SetCastHiddenShadow(bOriginalMeshCastHiddenShadow);
		GetMesh()->UnregisterOnBoneTransformsFinalizedDelegate(FirstPersonPoseDelegateHandle);
		FirstPersonPoseDelegateHandle.Reset();
		if (FirstPersonBody)
		{
			FirstPersonBody->DestroyComponent();
			FirstPersonBody = nullptr;
		}
		GetMesh()->VisibilityBasedAnimTickOption = OriginalMeshAnimTickOption;
	}

	bFirstPersonView = bEnabled;
	RefreshFirstPersonWorldWeapons();
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
	FollowCamera->SetWorldLocation(GetPawnViewLocation() + ViewYaw.RotateVector(FirstPersonCameraOffset));
}

void ABrokenEdgeCharacter::UpdateFirstPersonWeapon()
{
	RefreshFirstPersonWorldWeapons();
	if (!DedicatedFirstPersonMesh) return;
	const FVector AimOffset = FirstPersonWeaponFit.bEnabled
		                          ? FVector::ZeroVector
		                          : (IsAiming() ? FirstPersonAimMeshOffset : FVector::ZeroVector);
	DedicatedFirstPersonMesh->SetRelativeLocation(FMath::VInterpTo(DedicatedFirstPersonMesh->GetRelativeLocation(),
	                                                               FirstPersonMeshTransform.GetLocation() + AimOffset,
	                                                               GetWorld()->GetDeltaSeconds(),
	                                                               FirstPersonAimInterpSpeed));
	AWeapon* Weapon = GetEquippedWeapon();
	// Owner/state replication can arrive before the equipment slot is cleared.
	if (!IsValid(Weapon) || Weapon->GetOwner() != this || Weapon->GetWeaponState() == EWeaponState::EWS_Dropped)
		Weapon = nullptr;
	if (FirstPersonSourceWeapon != Weapon)
	{
		FirstPersonSourceWeapon = Weapon;
		if (Weapon)
		{
			if (!FirstPersonWeaponMesh)
			{
				FirstPersonWeaponMesh = NewObject<USkeletalMeshComponent>(this);
				FirstPersonWeaponMesh->SetupAttachment(FirstPersonWeaponFit.bEnabled
					                                       ? static_cast<USceneComponent*>(FollowCamera)
					                                       : static_cast<USceneComponent*>(DedicatedFirstPersonMesh),
				                                       FirstPersonWeaponFit.bEnabled
					                                       ? NAME_None
					                                       : FName(TEXT("RightHandSocket")));
				FirstPersonWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				FirstPersonWeaponMesh->SetGenerateOverlapEvents(false);
				FirstPersonWeaponMesh->SetOnlyOwnerSee(true);
				FirstPersonWeaponMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
				FirstPersonWeaponMesh->SetCastShadow(false);
				FirstPersonWeaponMesh->SetCanEverAffectNavigation(false);
				FirstPersonWeaponMesh->RegisterComponent();
			}
			USkeletalMesh* SourceMesh = Weapon->GetWeaponMesh()->GetSkeletalMeshAsset();
			FirstPersonWeaponMesh->SetSkeletalMeshAsset(
				FirstPersonWeaponVisualMesh && SourceMesh == FirstPersonWeaponSourceMesh
					? FirstPersonWeaponVisualMesh.Get()
					: SourceMesh);
			for (int32 Index = 0; Index < Weapon->GetWeaponMesh()->GetNumMaterials(); ++Index)
				FirstPersonWeaponMesh->SetMaterial(Index, Weapon->GetWeaponMesh()->GetMaterial(Index));
			FirstPersonWeaponMesh->SetRelativeTransform(FirstPersonWeaponTransform);
			FirstPersonWeaponMesh->AttachToComponent(UsesFirstPersonWeaponFit()
				                                         ? static_cast<USceneComponent*>(FollowCamera)
				                                         : static_cast<USceneComponent*>(DedicatedFirstPersonMesh),
			                                         FAttachmentTransformRules::KeepRelativeTransform,
			                                         UsesFirstPersonWeaponFit()
				                                         ? NAME_None
				                                         : FName(TEXT("RightHandSocket")));
			FirstPersonWeaponMesh->SetAnimInstanceClass(UsesFirstPersonWeaponFit()
				                                            ? UStingerFirstPersonWeaponAnimInstance::StaticClass()
				                                            : nullptr);
			FirstPersonWeaponMesh->AddTickPrerequisiteComponent(DedicatedFirstPersonMesh);
			ViewWeaponTarget = FirstPersonWeaponFit.Hip;
		}
	}
	if (UsesFirstPersonWeaponFit())
	{
		const FTransform& Target = IsAiming() ? FirstPersonWeaponFit.Aim : FirstPersonWeaponFit.Hip;
		ViewWeaponTarget.SetLocation(FMath::VInterpTo(ViewWeaponTarget.GetLocation(), Target.GetLocation(),
		                                              GetWorld()->GetDeltaSeconds(), FirstPersonAimInterpSpeed));
		ViewWeaponTarget.SetRotation(FMath::QInterpTo(ViewWeaponTarget.GetRotation(), Target.GetRotation(),
		                                              GetWorld()->GetDeltaSeconds(), FirstPersonAimInterpSpeed));
	}
	SyncFirstPersonAction();
	if (FirstPersonWeaponMesh) FirstPersonWeaponMesh->SetVisibility(Weapon && !bDefeated);
	DedicatedFirstPersonMesh->SetVisibility(Weapon && !bDefeated, false);
}

bool ABrokenEdgeCharacter::UsesFirstPersonWeaponFit() const
{
	return FirstPersonWeaponFit.bEnabled && FirstPersonSourceWeapon && FirstPersonWeaponMesh &&
		FirstPersonSourceWeapon->GetWeaponMesh()->GetSkeletalMeshAsset() == FirstPersonWeaponSourceMesh;
}

FTransform ABrokenEdgeCharacter::GetFirstPersonGripTarget(FName Socket) const
{
	if (!UsesFirstPersonWeaponFit() || !FirstPersonWeaponMesh->DoesSocketExist(Socket)) return FTransform::Identity;
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
		const FString Finish = Montage == FirstPersonReloadMontage
			                       ? TEXT("ReloadFinished")
			                       : Montage == FirstPersonSwapMontage
			                       ? TEXT("SwapFinished")
			                       : TEXT("");
		for (const FAnimNotifyEvent& Notify : ThirdPersonMontage->Notifies)
		{
			const float Time = Notify.GetTriggerTime() - ViewActionSectionStart;
			if (Time <= 0.f || Time > ViewActionSectionLength) continue;
			const FString Name = Notify.GetNotifyEventName().ToString();
			if (!Finish.IsEmpty() && Name.Contains(Finish)) ViewActionSectionLength = Time;
			if (Montage == FirstPersonSwapMontage && Name.Contains(TEXT("AttachWeapons")))
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
			           ? Fraction / Key * FirstPersonWeaponFit.SwapAttachFraction
			           : FirstPersonWeaponFit.SwapAttachFraction + (Fraction - Key) / (1.f - Key)
			           * (1.f - FirstPersonWeaponFit.SwapAttachFraction);
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
	if (!ViewActionMontage || ViewActionMontage != FirstPersonReloadMontage || !ViewActionThirdPersonMontage)
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
	bSprintRequested = bEnableSprint && bRequested;
}

void ABrokenEdgeCharacter::UpdateSprint()
{
	if (!bEnableSprint || (!HasAuthority() && !IsLocallyControlled()) || !Combat) return;
	const FVector Acceleration = GetCharacterMovement()->GetCurrentAcceleration();
	bSprinting = bSprintRequested && !bIsCrouched && !IsAiming() && !bDisableGameplay && !bDefeated
		&& GetCombatState() == ECombatState::ECS_Unoccuiped
		&& FVector::DotProduct(Acceleration.GetSafeNormal2D(), GetActorForwardVector()) > 0.5f;
	GetCharacterMovement()->MaxWalkSpeed = bSprinting
		                                       ? SprintSpeed
		                                       : (IsAiming() ? Combat->AimWalkSpeed : Combat->BaseWalkSpeed);
}

void ABrokenEdgeCharacter::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABrokenEdgeCharacter, bSprinting);

	DOREPLIFETIME_CONDITION(ABrokenEdgeCharacter, OverlappingWeapon, COND_OwnerOnly);
	// DOREPLIFETIME(ABrokenEdgeCharacter, OverlappingWeapon);
	DOREPLIFETIME(ABrokenEdgeCharacter, Health);
	DOREPLIFETIME(ABrokenEdgeCharacter, bDisableGameplay);
	DOREPLIFETIME(ABrokenEdgeCharacter, Shield);
}

void ABrokenEdgeCharacter::PostInitializeComponents()
{
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
	if (AnimInstance && SwapMontage)
	{
		AnimInstance->Montage_Play(SwapMontage);
		PlayFirstPersonMontage(FirstPersonSwapMontage, SwapMontage);
	}
}

/**
 * @brief 播放受击动画
 */
void ABrokenEdgeCharacter::PlayHitReactMontage()
{
	if (bDefeated || Combat == nullptr || HitReactMontage == nullptr) return;

	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (AnimInstance)
	{
		AnimInstance->Montage_Play(HitReactMontage);
		FName SectionName("FromRight");
		AnimInstance->Montage_JumpToSection(SectionName);
	}
}

void ABrokenEdgeCharacter::PlayReloadMontage()
{
	if (Combat == nullptr || Combat->EquippedWeapon == nullptr || ReloadMontage == nullptr) return;

	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (AnimInstance == nullptr)return;

	AnimInstance->Montage_Play(ReloadMontage);

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
	PlayFirstPersonMontage(FirstPersonReloadMontage, ReloadMontage, SectionName);
}

void ABrokenEdgeCharacter::PlayFireMontage(bool bAiming)
{
	if (Combat == nullptr || Combat->EquippedWeapon == nullptr || FireWeaponMontage == nullptr) return;

	if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
	{
		AnimInstance->Montage_Play(FireWeaponMontage);
		FName SectionName = bAiming ? FName("RifleAim") : FName("RifleHip");
		// UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
		//        TEXT(""));
		AnimInstance->Montage_JumpToSection(SectionName);
		PlayFirstPersonMontage(FirstPersonFireMontage, FireWeaponMontage, SectionName);
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
void ABrokenEdgeCharacter::Jump()
{
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

void ABrokenEdgeCharacter::Move(const FInputActionValue& Value)
{
	if (bDisableGameplay) return;
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

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
	if (Combat && Combat->bHoldingTheFlag) return;
	if (bDisableGameplay) return;
	if (Combat == nullptr) return;

	if (bIsCrouched)
	{
		UnCrouch();
	}
	else
	{
		Crouch();
	}
}

void ABrokenEdgeCharacter::EquipButtonPressed()
{
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
	if (Combat && Combat->bHoldingTheFlag) return;
	if (bDisableGameplay) return;
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
	DropWeapons();
	MulticastDefeated(bPlayerLeftGame);
}

void ABrokenEdgeCharacter::MulticastDefeated_Implementation(bool bPlayerLeftGame)
{
	SetFirstPersonView(false);
	bLeftGame = bPlayerLeftGame;

	if (BrokenEdgePlayerController)
	{
		BrokenEdgePlayerController->SetHUDWeaponAmmo(0);
	}
	bDefeated = true;

	PlayDefeatMontage();

	// 禁用角色移动
	bDisableGameplay = true;
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
