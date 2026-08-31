// Fill out your copyright notice in the Description page of Project Settings.


#include "BrokenEdgeCharacter.h"

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

// Called every frame
void ABrokenEdgeCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
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
		AO_X = DeltaAimRotation.Yaw;
		if (TurningInPlace == ETurningInPlace::ETIP_NotTurning)
		{
			InterpAO_X = AO_X;
		}
		bUseControllerRotationYaw = true;
		TurnInPlace(DeltaTime);
	}

	if (Speed > 0.f || bIsInAir) // 跑或者跳
	{
		bRotateRootBone = false;
		StartingAimRotation = FRotator(0.f, GetBaseAimRotation().Yaw, 0.f);
		AO_X = 0.f;
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
	AO_Y = GetBaseAimRotation().Pitch;
	if (AO_Y > 90.f && !IsLocallyControlled())
	{
		// 将map俯仰角从 [270, 360) 转换为 [-90, 0)
		FVector2D InRange(270.f, 360.f);
		FVector2D OutRange(-90.f, 0.f);
		AO_Y = FMath::GetMappedRangeValueClamped(InRange, OutRange, AO_Y);
	}
}

/**
 * @brief 原地转向插值处理
 * @param DeltaTime 帧时间
 */
void ABrokenEdgeCharacter::TurnInPlace(float DeltaTime)
{
	if (AO_X > 90.f)
	{
		TurningInPlace = ETurningInPlace::ETIP_Right;
	}
	else if (AO_X < -90.f)
	{
		TurningInPlace = ETurningInPlace::ETIP_Left;
	}
	if (TurningInPlace != ETurningInPlace::ETIP_NotTurning)
	{
		InterpAO_X = FMath::FInterpTo(InterpAO_X, 0.f, DeltaTime, 4.f);
		AO_X = InterpAO_X;
		if (FMath::Abs(AO_X) < 15.f)
		{
			TurningInPlace = ETurningInPlace::ETIP_NotTurning;
			StartingAimRotation = FRotator(0.f, GetBaseAimRotation().Yaw, 0.f);
		}
	}
}

void ABrokenEdgeCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

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

void ABrokenEdgeCharacter::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

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
	}
}

/**
 * @brief 播放受击动画
 */
void ABrokenEdgeCharacter::PlayHitReactMontage()
{
	if (bDefeated||Combat == nullptr || HitReactMontage == nullptr) return;

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
}

void ABrokenEdgeCharacter::PlayFireMontage(bool bAiming)
{
	if (Combat == nullptr || Combat->EquippedWeapon == nullptr || FireWeaponMontage == nullptr) return;

	if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
	{
		AnimInstance->Montage_Play(FireWeaponMontage);
		FName SectionName = bAiming ? FName("RifleAim") : FName("RifleHip");
		UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
		       TEXT(""));
		AnimInstance->Montage_JumpToSection(SectionName);
	}
}

void ABrokenEdgeCharacter::PlayDefeatMontage()
{
	if (DefeatedMontage==nullptr)return;
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
	BrokenEdgeGameMode = BrokenEdgeGameMode == nullptr ?
	GetWorld()->GetAuthGameMode<ABrokenEdgeGameMode>() : BrokenEdgeGameMode;

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
		Combat->EquippedWeapon=nullptr;

	}
	if (Combat->SecondaryWeapon)
	{
		Combat->SecondaryWeapon->Dropped();
		Combat->SecondaryWeapon=nullptr;

	}

}
