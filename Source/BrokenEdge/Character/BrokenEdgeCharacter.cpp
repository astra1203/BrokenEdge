// Fill out your copyright notice in the Description page of Project Settings.


#include "BrokenEdgeCharacter.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "BrokenEdge/BrokenEdge.h"
#include "BrokenEdge/BrokenEdgeTypes/TurningInPlace.h"
#include "BrokenEdge/Component/BuffComponent.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
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

// Called when the game starts or when spawned
void ABrokenEdgeCharacter::BeginPlay()
{
	Super::BeginPlay();

}

// Called every frame
void ABrokenEdgeCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	//@TODO
	PollInit();
}

void ABrokenEdgeCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	// Add Input Mapping Context
	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
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
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ABrokenEdgeCharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ABrokenEdgeCharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ABrokenEdgeCharacter::Move);

		//看
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ABrokenEdgeCharacter::Look);

		//蹲伏
		EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Triggered, this, &ABrokenEdgeCharacter::CrouchButtonPressed);
		
		//装备武器
		EnhancedInputComponent->BindAction(EquipAction, ETriggerEvent::Triggered, this, &ABrokenEdgeCharacter::EquipButtonPressed);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}

}

void ABrokenEdgeCharacter::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(ABrokenEdgeCharacter, OverlappingWeapon, COND_OwnerOnly);
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

	//@TODO
	//延迟补偿组件

}

void ABrokenEdgeCharacter::PlaySwapMontage()
{
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (AnimInstance && SwapMontage)
	{
		AnimInstance->Montage_Play(SwapMontage);
	}
}

void ABrokenEdgeCharacter::UpdateHUDHealth()
{
	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, Controller?*Controller->GetName():TEXT("空"));
	BrokenEdgePlayerController = BrokenEdgePlayerController == nullptr ? Cast<ABrokenEdgePlayerController>(GetController()) : BrokenEdgePlayerController;

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
	BrokenEdgePlayerController = BrokenEdgePlayerController == nullptr ? 
	Cast<ABrokenEdgePlayerController>(Controller) : BrokenEdgePlayerController;
	if (BrokenEdgePlayerController)
	{
		BrokenEdgePlayerController->SetHUDShield(Shield, MaxShield);
	}
}

/**
 * @brief 跳跃处理
 */
void ABrokenEdgeCharacter::Jump()
{
	// @ TODO
	// if (Combat && Combat->bHoldingTheFlag) return;
	// if (Combat) return;
	// @ TODO
	// if (bDisableGameplay) return;
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
	// @TODO
	// if (Combat && Combat->bHoldingTheFlag) return;
	// if (bDisableGameplay) return;
	// if (Combat)return;

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
	//@TODO
	if (Combat)
	{
		if (Combat->bHoldingTheFlag) return;
		if (Combat->CombatState == ECombatState::ECS_Unoccuiped) ServerEquipButtonPressed();
		bool bSwap = Combat->ShouldSwapWeapons() &&
			!HasAuthority() &&
			Combat->CombatState == ECombatState::ECS_Unoccuiped &&
			OverlappingWeapon == nullptr;

		//@TODO
		UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
		       bSwap?TEXT("切换武器"):TEXT("不切换武器"),Combat->ShouldSwapWeapons()?TEXT("是切换武器"):TEXT("不是切换武器"));

		UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s %s %s %s %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
				bSwap?TEXT("切换武器"):TEXT("不切换武器"),
				Combat->ShouldSwapWeapons()?TEXT("有副武器和主武器都存在"):TEXT("没有副武器或主武器或武器"),
				!HasAuthority()?TEXT("客户端点击"):TEXT("服务器点击"),
		       Combat->CombatState == ECombatState::ECS_Unoccuiped?TEXT("未占用"):TEXT("已占用"),
		       OverlappingWeapon == nullptr?TEXT("没有重叠武器"):TEXT("有重叠武器"));


		if (bSwap)
		{
			PlaySwapMontage();
			Combat->CombatState = ECombatState::ECS_SwappingWeapon;
			// bFinishedSwapping = false;
		}
	}


}

void ABrokenEdgeCharacter::OnRep_OverlappingWeapon(AWeapon* LastWeapon)
{
	//@TODO
}

/**
 * @brief 服务器端装备按钮按下（RPC实现）
 */
void ABrokenEdgeCharacter::ServerEquipButtonPressed_Implementation()
{
	//@TODO
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
	//@TODO
	// UpdateHUDShield();
}

void ABrokenEdgeCharacter::OnRep_Health()
{
	UpdateHUDHealth();
	//@TODO
	
	
}

void ABrokenEdgeCharacter::PollInit()
{
	//@TODO
	
	
	if (BrokenEdgePlayerController==nullptr)
	{
		BrokenEdgePlayerController = Cast<ABrokenEdgePlayerController>(Controller);
		if (BrokenEdgePlayerController)
		{
			
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
	//@ TODO
	return true;
}

/**
 * @brief 是否正在瞄准
 * @return 是否瞄准
 */
bool ABrokenEdgeCharacter::IsAiming()
{
	//@ TODO
	return true;
	// return (Combat && Combat->bAiming);
}
