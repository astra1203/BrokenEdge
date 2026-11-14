// Fill out your copyright notice in the Description page of Project Settings.


#include "CombatComponent.h"
#include "BrokenEdge/Character/BrokenEdgeCharacterPresentation.h"
#include "CombatModeAdapter.h"
#include "BrokenEdge/Extraction/Combat/ExtractionMeleeComponent.h"
#include "BrokenEdge/Extraction/Combat/ExtractionCombatComponent.h"
#include "BrokenEdge/Extraction/Core/ExtractionInteractionComponent.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/Core/ExtractionPlayerController.h"

#include "BrokenEdge/BrokenEdge.h"
#include "BrokenEdge/BrokenEdgeTypes/WeaponType.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "BrokenEdge/Weapon/Shotgun.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "BrokenEdge/Weapon/WeaponPresentation.h"
#include "Camera/CameraComponent.h"
#include "Engine/SkeletalMeshSocket.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"


// Sets default values for this component's properties
UCombatComponent::UCombatComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...

	BaseWalkSpeed = 600.f;
	AimWalkSpeed = 450.f;
}

void UCombatComponent::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCombatComponent, EquippedWeapon);
	DOREPLIFETIME(UCombatComponent, SecondaryWeapon);
	DOREPLIFETIME(UCombatComponent, bAiming);
	DOREPLIFETIME_CONDITION(UCombatComponent, CarriedAmmo, COND_OwnerOnly);
	DOREPLIFETIME(UCombatComponent, bHoldingTheFlag);
	DOREPLIFETIME(UCombatComponent, CombatState);

	DOREPLIFETIME(UCombatComponent, TheFlag);

	//@TODO
}

/**
 * @brief 装备武器
 * @param WeaponToEquip 要装备的武器
 */
void UCombatComponent::EquipWeapon(class AWeapon* WeaponToEquip)
{
	// Extraction equipment is created only from authoritative item instances.
	if (Character && Character->GetExtractionCombat()->IsEnabled()) return;
	if (Character == nullptr || WeaponToEquip == nullptr) return;
	if (CombatState != ECombatState::ECS_Unoccuiped) return;
	WeaponToEquip->MulticastAmmo(WeaponToEquip->GetAmmo());

	//拾取的是旗帜的情况下
	if (WeaponToEquip->GetWeaponType() == EWeaponType::EWT_Flag)
	{
		Character->Crouch();
		bHoldingTheFlag = true;
		WeaponToEquip->SetWeaponState(EWeaponState::EWS_Equipped);
		AttachFlagToLeftHand(WeaponToEquip);
		WeaponToEquip->SetOwner(Character);
		TheFlag = WeaponToEquip;
	}
	else
	{
		if (EquippedWeapon != nullptr && SecondaryWeapon == nullptr)
		{
			EquipSecondaryWeapon(WeaponToEquip);
		}
		else //主武器没装备或者主武器和副武器都装备了
		{
			EquipPrimaryWeapon(WeaponToEquip);
		}

		Character->GetCharacterMovement()->bOrientRotationToMovement = false;
		Character->bUseControllerRotationYaw = true;
	}
}

void UCombatComponent::SwapWeapons()
{
	UE_LOG(LogTemp, Verbose, TEXT("EXTRACTION_SWAP Request Owner=%s State=%d"), *GetNameSafe(Character),
	       static_cast<int32>(GetState()));
	if (Character && Character->GetExtractionCombat()->IsEnabled())
	{
		Character->GetExtractionCombat()->BeginServerSwap();
		return;
	}
	if (CombatState != ECombatState::ECS_Unoccuiped || !Character || !Character->HasAuthority()
		|| Character->IsDefeated() || Character->bDisableGameplay || !EquippedWeapon || !SecondaryWeapon) return;

	Character->PlaySwapMontage();
	CombatState = ECombatState::ECS_SwappingWeapon;
	bSwapAttachPending = true;
	Character->bFinishedSwapping = false;
	if (SecondaryWeapon) SecondaryWeapon->EnableCustomDepth(false);
}


void UCombatComponent::FinishSwap()
{
	UE_LOG(LogTemp, Verbose, TEXT("EXTRACTION_SWAP Finish Owner=%s State=%d"), *GetNameSafe(Character),
	       static_cast<int32>(GetState()));
	if (Character && Character->GetExtractionCombat()->IsEnabled()) return;
	if (CombatState != ECombatState::ECS_SwappingWeapon) return;
	bSwapAttachPending = false;
	if (Character && Character->HasAuthority())
	{
		CombatState = ECombatState::ECS_Unoccuiped;
	}

	if (Character) Character->bFinishedSwapping = true;

	if (SecondaryWeapon) SecondaryWeapon->EnableCustomDepth(true);
}

void UCombatComponent::FinishSwapAttachWeapons()
{
	UE_LOG(LogTemp, Verbose, TEXT("EXTRACTION_SWAP Attach Owner=%s State=%d"), *GetNameSafe(Character),
	       static_cast<int32>(GetState()));
	if (Character && Character->GetExtractionCombat()->IsEnabled()) return;
	if (!Character || !Character->HasAuthority() || CombatState != ECombatState::ECS_SwappingWeapon || !bSwapAttachPending) return;
	bSwapAttachPending = false;
	AttachSwappedWeapons();
}

void UCombatComponent::AttachSwappedWeapons()
{
	if (!EquippedWeapon || !SecondaryWeapon) return;
	if (Character == nullptr || !Character->HasAuthority()) return;
	if (Character->GetExtractionCombat()->IsEnabled() &&
		(!Character->GetExtractionCombat()->CanAct() || GetState() != ECombatState::ECS_SwappingWeapon))
		return;
	// PlayEquipWeaponSound(SecondaryWeapon);//@TODO
	AWeapon* TempWeapon = EquippedWeapon;
	EquippedWeapon = SecondaryWeapon;
	SecondaryWeapon = TempWeapon;

	EquippedWeapon->SetWeaponState(EWeaponState::EWS_Equipped);
	AttachActorToRightHand(EquippedWeapon);
	EquippedWeapon->SetHUDAmmo();
	UpdateCarriedAmmo();

	SecondaryWeapon->SetWeaponState(EWeaponState::EWS_EquippedSecondary);
	AttachActorToBackpack(SecondaryWeapon);
	Character->RefreshFirstPersonWorldWeapons();
	if (Character->GetExtractionInteraction()->IsEnabled())
		if (auto* Inv = Character->GetExtractionInteraction()->Inventory()) Inv->CaptureWeapons();
}


/**
 * @brief 换弹逻辑
 */
void UCombatComponent::Reload()
{
	ModeAdapter().Reload(*this);
}

void UCombatComponent::FinishReloading()
{
	ModeAdapter().FinishReload(*this);
}

void UCombatComponent::ShotgunReloadOnce()
{
	if (Character && Character->GetExtractionCombat()->IsEnabled()) return;
	if (Character && Character->HasAuthority())
	{
		UpdateShotgunAmmoValues();
	}
}


void UCombatComponent::Drop()
{
	ModeAdapter().Drop(*this);
}

/**
 * @brief 开火按钮按下处理
 * @param bPressed 是否按下
 */
void UCombatComponent::FireButtonPressed(bool bPressed)
{
	ModeAdapter().SetFireHeld(*this, bPressed);
}


/**
 * @brief 开火逻辑
 */
void UCombatComponent::Fire()
{
	if (CanFire())
	{
		if (EquippedWeapon == nullptr)return;
		// Lock the active mode before issuing a shot, including synchronous authority callbacks.
		StartFireTimer();

		if (!Character || !Character->GetExtractionCombat()->IsEnabled()) NotifyLocalShot();

		switch (EquippedWeapon->FireType)
		{
		case EFireType::EFT_Projectile:
			FireProjectileWeapon();
			break;
		case EFireType::EFT_HitScan:
			FireHitScanWeapon();
			break;
		case EFireType::EFT_Shotgun:
			FireShotgunWeapon();
			break;
		}
	}
}

void UCombatComponent::NotifyLocalShot()
{
	if (Character && Character->IsLocallyControlled())
	{
		CrosshairShootingFactor = FMath::Min(2.f, CrosshairShootingFactor + .65f);
		bFreshCrosshairShot = true;
	}
}

void UCombatComponent::FireProjectileWeapon()
{
	if (Character && Character->GetExtractionCombat()->IsEnabled()) return;
	if (EquippedWeapon == nullptr || Character == nullptr) return;

	HitTarget = EquippedWeapon->bUseScatter ? EquippedWeapon->TraceEndWithScatter(HitTarget) : HitTarget;

	if (!Character->HasAuthority()) LocalFire(HitTarget);
	ServerFire(HitTarget, EquippedWeapon->FireDelay);
}


void UCombatComponent::FireHitScanWeapon()
{
	if (Character && Character->GetExtractionCombat()->IsEnabled())
	{
		Character->GetExtractionCombat()->RequestShot(HitTarget);
		return;
	}
	if (EquippedWeapon == nullptr || Character == nullptr) return;

	HitTarget = EquippedWeapon->bUseScatter ? EquippedWeapon->TraceEndWithScatter(HitTarget) : HitTarget;

	if (!Character->HasAuthority()) LocalFire(HitTarget);
	ServerFire(HitTarget, EquippedWeapon->FireDelay);
}

void UCombatComponent::FireShotgunWeapon()
{
	if (Character && Character->GetExtractionCombat()->IsEnabled()) return;
	AShotgun* Shotgun = Cast<AShotgun>(EquippedWeapon);
	if (Shotgun == nullptr || Character == nullptr)return;

	TArray<FVector_NetQuantize> HitTargets;
	Shotgun->ShotgunTraceEndWithScatter(HitTarget, HitTargets);

	if (!Character->HasAuthority()) ShotgunLocalFire(HitTargets);
	ServerShotgunFire(HitTargets, EquippedWeapon->FireDelay);
}

/**
 * @brief 本地霰弹枪开火
 * @param TraceHitTargets 命中目标位置数组
 */
void UCombatComponent::ShotgunLocalFire(const TArray<FVector_NetQuantize>& TraceHitTargets)
{
	AShotgun* Shotgun = Cast<AShotgun>(EquippedWeapon);
	if (Shotgun == nullptr || Character == nullptr) return;

	if (CombatState == ECombatState::ECS_Unoccuiped || CombatState == ECombatState::ECS_Reloading)
	{
		bLocallyReloading = false;

		Character->PlayFireMontage(bAiming);
		Shotgun->FireShotgun(TraceHitTargets);
		CombatState = ECombatState::ECS_Unoccuiped;
	}
}


void UCombatComponent::ServerShotgunFire_Implementation(const TArray<FVector_NetQuantize>& TraceHitTargets,
                                                        float FireDelay)
{
	if (Character && Character->GetExtractionCombat()->IsEnabled()) return;
	MulticastShotgunFire(TraceHitTargets);
}

bool UCombatComponent::ServerShotgunFire_Validate(const TArray<FVector_NetQuantize>& TraceHitTargets, float FireDelay)
{
	if (EquippedWeapon)
	{
		bool bNearlyEqual = FMath::IsNearlyEqual(EquippedWeapon->FireDelay, FireDelay, 0.001f);
		return bNearlyEqual;
	}
	return true;
}


void UCombatComponent::MulticastShotgunFire_Implementation(const TArray<FVector_NetQuantize>& TraceHitTargets)
{
	if (Character && Character->IsLocallyControlled() && !Character->HasAuthority()) return;
	ShotgunLocalFire(TraceHitTargets);
}

/**
 * @brief 服务器端开火（RPC实现）
 * @param TraceHitTarget 命中目标位置
 * @param FireDelay 开火延迟
 */
void UCombatComponent::ServerFire_Implementation(const FVector_NetQuantize& TraceHitTarget, float FireDelay)
{
	if (Character && Character->GetExtractionCombat()->IsEnabled()) return;
	MulticastFire(TraceHitTarget);
}

/**
 * @brief 服务器端开火验证（RPC验证）
 * @param TraceHitTarget 命中目标位置
 * @param FireDelay 开火延迟
 * @return 是否验证通过
 */
bool UCombatComponent::ServerFire_Validate(const FVector_NetQuantize& TraceHitTarget, float FireDelay)
{
	if (EquippedWeapon)
	{
		bool bNearlyEqual = FMath::IsNearlyEqual(EquippedWeapon->FireDelay, FireDelay, 0.001f);
		return bNearlyEqual;
	}
	return true;
}

void UCombatComponent::MulticastFire_Implementation(const FVector_NetQuantize& TraceHitTarget)
{
	if (Character && Character->IsLocallyControlled() && !Character->HasAuthority()) return;

	LocalFire(TraceHitTarget);
}

/**
 * @brief 本地开火
 * @param TraceHitTarget 命中目标位置
 */
void UCombatComponent::LocalFire(const FVector_NetQuantize& TraceHitTarget)
{
	// UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, *PrintNetMode());
	if (EquippedWeapon == nullptr) return;
	if (Character && CombatState == ECombatState::ECS_Unoccuiped)
	{
		Character->PlayFireMontage(bAiming);
		EquippedWeapon->Fire(TraceHitTarget);
	}
}

/**
 * @brief 判断是否可以开火
 * @return 是否可以开火
 */
bool UCombatComponent::CanFire()
{
	if (Character && Character->Melee->Active) return false;
	return ModeAdapter().CanFire(*this);
}

/**
 * @brief 启动开火计时器
 */
void UCombatComponent::StartFireTimer()
{
	ModeAdapter().StartFireTimer(*this);
}

/**
 * @brief 开火计时器完成
 */
void UCombatComponent::FireTimerFinished()
{
	bCanFire = true;
	// A pending legacy timer must never drive the extraction protocol after a mode change.
	if (!Character || Character->GetExtractionCombat()->IsEnabled() || !EquippedWeapon) return;
	if (IsFireInputHeld() && EquippedWeapon->bAutomatic)
	{
		Fire();
	}
	ReloadEmptyWeapon();
}

/**
 * @brief 当武器弹药为空时自动换弹
 */
void UCombatComponent::ReloadEmptyWeapon()
{
	if (EquippedWeapon && EquippedWeapon->IsEmpty())
	{
		Reload();
	}
}


/**
 * @brief 更新弹药数值
 */
void UCombatComponent::UpdateAmmoValues()
{
	if (Character == nullptr || EquippedWeapon == nullptr) return;
	int32 ReloadAmount = AmountToReload();
	if (CarriedAmmoMap.Contains(EquippedWeapon->GetWeaponType()))
	{
		CarriedAmmoMap[EquippedWeapon->GetWeaponType()] -= ReloadAmount;
		CarriedAmmo = CarriedAmmoMap[EquippedWeapon->GetWeaponType()];
	}
	Controller = Controller == nullptr ? Cast<ABrokenEdgePlayerController>(Character->Controller) : Controller;
	if (Controller)
	{
		Controller->SetHUDCarriedAmmo(CarriedAmmo);
	}
	EquippedWeapon->AddAmmo(ReloadAmount);
}


void UCombatComponent::UpdateShotgunAmmoValues()
{
	if (!Character || !Character->HasAuthority() || Character->IsDefeated() || !EquippedWeapon
		|| CombatState != ECombatState::ECS_Reloading || EquippedWeapon->GetWeaponType() != EWeaponType::EWT_Shotgun) return;
	int32* Reserve = CarriedAmmoMap.Find(EWeaponType::EWT_Shotgun);
	if (!Reserve || *Reserve <= 0 || EquippedWeapon->IsFull()) { JumpToShotgunEnd(); return; }
	CarriedAmmo = --*Reserve;
	Controller = Controller == nullptr ? Cast<ABrokenEdgePlayerController>(Character->Controller) : Controller;
	if (Controller)
	{
		Controller->SetHUDCarriedAmmo(CarriedAmmo);
	}
	EquippedWeapon->AddAmmo(1);

	bCanFire = true;

	if (EquippedWeapon->IsFull() || CarriedAmmo == 0)
	{
		JumpToShotgunEnd();
	}
}

/**
 * @brief 跳转到霰弹枪换弹动画结束
 */
void UCombatComponent::JumpToShotgunEnd()
{
	UAnimInstance* AnimInstance = Character->GetMesh()->GetAnimInstance();
	if (AnimInstance && Character->GetReloadMontage())
	{
		AnimInstance->Montage_JumpToSection(FName("ShotgunEnd"));
	}
}

/**
 * @brief 计算换弹数量
 * @return 要换入弹匣的弹药数量
 */
int32 UCombatComponent::AmountToReload()
{
	if (EquippedWeapon == nullptr) return 0;
	int32 RoomInMag = EquippedWeapon->GetMagCapacity() - EquippedWeapon->GetAmmo();

	if (CarriedAmmoMap.Contains(EquippedWeapon->GetWeaponType()))
	{
		int32 AmountCarried = CarriedAmmoMap[EquippedWeapon->GetWeaponType()];
		int32 Least = FMath::Min(RoomInMag, AmountCarried);
		return FMath::Clamp(RoomInMag, 0, Least);
	}
	return 0;
}

bool UCombatComponent::CanStartLegacyReload() const
{
	return Character && !Character->IsDefeated() && !Character->bDisableGameplay
		&& CombatState == ECombatState::ECS_Unoccuiped && EquippedWeapon
		&& !EquippedWeapon->IsFull() && CarriedAmmo > 0;
}

void UCombatComponent::ServerReload_Implementation()
{
	if (Character && Character->GetExtractionCombat()->IsEnabled())
	{
		Character->GetExtractionCombat()->BeginServerReload();
		return;
	}
	// Requests can arrive after the owner's idle/ammo snapshot has become stale.
	if (!CanStartLegacyReload()) return;

	CombatState = ECombatState::ECS_Reloading;

	// UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), 
	// 	GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT(""));

	if (!Character->IsLocallyReloading())HandleReload();
}

// Called when the game starts
void UCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	if (Character)
	{
		Character->GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;

		if (Character->GetFollowCamera())
		{
			DefaultFOV = Character->GetFollowCamera()->FieldOfView;
			CurrentFOV = DefaultFOV;
		}

		if (Character->HasAuthority())
		{
			InitializeCarriedAmmo();
		}
	}
}


// Called every frame
void UCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                     FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT(""));
	if (Character && Character->IsLocallyControlled())
	{
		FHitResult HitResult;
		TraceUnderCrosshairs(HitResult);
		HitTarget = HitResult.ImpactPoint;

		SetHUDCrosshairs(DeltaTime);
		InterpFOV(DeltaTime);
	}
}

int32 UCombatComponent::PickupAmmo(EWeaponType WeaponType, int32 AmmoAmount)
{
	if (Character && Character->GetExtractionCombat()->IsEnabled()) return AmmoAmount;
	int32 remaining = 0;
	if (CarriedAmmoMap.Contains(WeaponType))
	{
		int32 OldCarriedAmmo = CarriedAmmoMap[WeaponType];
		CarriedAmmoMap[WeaponType] = FMath::Clamp(CarriedAmmoMap[WeaponType] + AmmoAmount, 0, MaxCarriedAmmo);

		remaining = AmmoAmount - (CarriedAmmoMap[WeaponType] - OldCarriedAmmo);


		UpdateCarriedAmmo();
	}

	if (EquippedWeapon && EquippedWeapon->IsEmpty() && EquippedWeapon->GetWeaponType() == WeaponType)
	{
		Reload();
	}
	return remaining;
}

void UCombatComponent::DropEquippedWeapon()
{
	if (EquippedWeapon)
	{
		EquippedWeapon->Dropped();
	}
}

void UCombatComponent::AttachFlagToLeftHand(AActor* Flag)
{
	if (Character == nullptr || Character->GetMesh() == nullptr || Flag == nullptr) return;

	if (const USkeletalMeshSocket* HandSocket = Character->GetMesh()->GetSocketByName(FName("FlagSocket")))
	{
		HandSocket->AttachActor(Flag, Character->GetMesh());
	}
}

void UCombatComponent::AttachActorToRightHand(AActor* ActorToAttach)
{
	if (Character == nullptr || Character->GetMesh() == nullptr || ActorToAttach == nullptr) return;
	if (const USkeletalMeshSocket* HandSocket = Character->GetMesh()->GetSocketByName(Character->GetPresentation().WeaponHandSocket))
	{
		HandSocket->AttachActor(ActorToAttach, Character->GetMesh());
		if (const auto* Weapon = Cast<AWeapon>(ActorToAttach); Weapon && Weapon->Presentation)
			ActorToAttach->SetActorRelativeTransform(Weapon->Presentation->WorldHandTransform);
	}
}

void UCombatComponent::AttachActorToBackpack(AActor* ActorToAttach)
{
	if (Character == nullptr || Character->GetMesh() == nullptr || ActorToAttach == nullptr) return;
	if (const USkeletalMeshSocket* BackpackSocket = Character->GetMesh()->GetSocketByName(Character->GetPresentation().WeaponBackSocket))
	{
		BackpackSocket->AttachActor(ActorToAttach, Character->GetMesh());
		if (const auto* Weapon = Cast<AWeapon>(ActorToAttach); Weapon && Weapon->Presentation)
			ActorToAttach->SetActorRelativeTransform(Weapon->Presentation->WorldBackTransform);
	}
}

void UCombatComponent::EquipPrimaryWeapon(AWeapon* WeaponToEquip)
{
	if (WeaponToEquip == nullptr) return;
	DropEquippedWeapon();
	EquippedWeapon = WeaponToEquip;
	EquippedWeapon->SetWeaponState(EWeaponState::EWS_Equipped);
	AttachActorToRightHand(EquippedWeapon);
	EquippedWeapon->SetOwner(Character);
	Character->RefreshFirstPersonWorldWeapons();
	EquippedWeapon->SetHUDAmmo(); // 聆听服务器才会执行
	//@TODO
	UpdateCarriedAmmo();
	// PlayEquipWeaponSound(WeaponToEquip);
	// ReloadEmptyWeapon();
}

void UCombatComponent::EquipSecondaryWeapon(AWeapon* WeaponToEquip)
{
	if (WeaponToEquip == nullptr) return;

	SecondaryWeapon = WeaponToEquip;
	SecondaryWeapon->SetWeaponState(EWeaponState::EWS_EquippedSecondary);

	AttachActorToBackpack(WeaponToEquip);
	// PlayEquipWeaponSound(WeaponToEquip);//@TODO
	SecondaryWeapon->SetOwner(Character);
	Character->RefreshFirstPersonWorldWeapons();
}

void UCombatComponent::SetAiming(bool bIsAiming)
{
	if (bIsAiming && Character && Character->IsTraversing()) return;
	if (Character == nullptr || EquippedWeapon == nullptr) return;
	bAiming = bIsAiming;
	ServerSetAiming(bIsAiming);
	if (Character)
	{
		Character->GetCharacterMovement()->MaxWalkSpeed = bIsAiming ? AimWalkSpeed : BaseWalkSpeed;
	}

	if (Character->IsLocallyControlled() && EquippedWeapon->GetWeaponType() == EWeaponType::EWT_SniperRifle)
	{
		Character->ShowSniperScopeWidget(bIsAiming);
	}

	if (Character->IsLocallyControlled()) bAimButtonPressed = bIsAiming;
}

void UCombatComponent::TraceUnderCrosshairs(FHitResult& TraceHitResult)
{
	const auto* OwningController = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!OwningController || !OwningController->IsLocalController()) return;
	int32 Width = 0, Height = 0;
	OwningController->GetViewportSize(Width, Height);
	FVector CrosshairWorldPosition;
	FRotator ViewRotation;
	OwningController->GetPlayerViewPoint(CrosshairWorldPosition, ViewRotation);
	FVector CrosshairWorldDirection = ViewRotation.Vector();
	// A local controller can temporarily have no viewport (including NullRHI).
	// Never leave HitTarget at its previous/default position in that case.
	if (Width > 0 && Height > 0)
	{
		FVector ScreenPosition, ScreenDirection;
		if (UGameplayStatics::DeprojectScreenToWorld(OwningController, FVector2D(Width / 2.f, Height / 2.f),
		                                             ScreenPosition, ScreenDirection))
		{
			CrosshairWorldPosition = ScreenPosition;
			CrosshairWorldDirection = ScreenDirection;
		}
	}

	{
		if (Character && Character->GetExtractionCombat()->IsEnabled() && Character->GetExtractionCombat()->
			GetWeaponStats().IsValid())
			CrosshairWorldDirection = Character->GetControlRotation().Vector();
		FVector Start = CrosshairWorldPosition;
		FCollisionQueryParams QueryParams;

		if (Character && Character->IsFirstPersonView())
		{
			// Keep nearby targets hittable without tracing into our own body or weapons.
			QueryParams.AddIgnoredActor(Character);
			if (EquippedWeapon) QueryParams.AddIgnoredActor(EquippedWeapon);
			if (SecondaryWeapon) QueryParams.AddIgnoredActor(SecondaryWeapon);
		}
		else if (Character)
		{
			float DistanceToCharacter = (Character->GetActorLocation() - Start).Size();
			Start += CrosshairWorldDirection * (DistanceToCharacter + 100.f);
		}

		FVector End = Start + CrosshairWorldDirection * TRACE_LENGTH;

		bool bHIt = GetWorld()->LineTraceSingleByChannel(
			TraceHitResult,
			Start,
			End,
			ECollisionChannel::ECC_Visibility,
			QueryParams
		);

		if (bHIt)
		{
			//@TODO
			// if (TraceHitResult.GetActor() && TraceHitResult.GetActor()->Implements<UInteractWithCrosshairsInterface>())
			if (const ABrokenEdgeCharacter* CharacterHit = Cast<ABrokenEdgeCharacter>(TraceHitResult.GetActor()))
			{
				HUDPackage.CrosshairsColor = FLinearColor::Red;
			}
			else
			{
				HUDPackage.CrosshairsColor = FLinearColor::White;
			}
		}
		else
		{
			TraceHitResult.ImpactPoint = FVector_NetQuantize(End);
		}
	}
}

void UCombatComponent::SetHUDCrosshairs(float DeltaTime)
{
	if (Character == nullptr || Character->Controller == nullptr) return;

	Controller = Controller == nullptr ? Cast<ABrokenEdgePlayerController>(Character->Controller) : Controller;
	if (Controller == nullptr)return;

	HUD = HUD == nullptr ? Cast<ABrokenEdgeHUD>(Controller->GetHUD()) : HUD;
	if (HUD == nullptr)return;

	// 举枪瞄准时隐藏屏幕准星：机械瞄具和红点已经把瞄准基准放到枪上，
	// 叠加的屏幕十字会盖住照门缺口与前准星，反而看不清瞄准关系。
	const bool AimDownSights = EquippedWeapon && Character->GetExtractionCombat()->IsEnabled()
		&& Character->IsFirstPersonView() && bAiming && !Character->Melee->Active;
	HUDPackage.bProceduralCrosshair = EquippedWeapon && Character->GetExtractionCombat()->IsEnabled()
		&& !AimDownSights && !Character->Melee->Active && Character->GetExtractionCombat()->CanAct()
		&& !Character->GetExtractionInteraction()->InventoryOpen && !Character->IsSprinting();
	HUDPackage.AngularSpread = Character->GetExtractionCombat()->GetCrosshairAngularSpread();
	if (EquippedWeapon && !AimDownSights && (!Character->GetExtractionCombat()->IsEnabled() || HUDPackage.
		bProceduralCrosshair))
	{
		HUDPackage.CrosshairsCenter = EquippedWeapon->CrosshairsCenter;
		HUDPackage.CrosshairsLeft = EquippedWeapon->CrosshairsLeft;
		HUDPackage.CrosshairsRight = EquippedWeapon->CrosshairsRight;
		HUDPackage.CrosshairsBottom = EquippedWeapon->CrosshairsBottom;
		HUDPackage.CrosshairsTop = EquippedWeapon->CrosshairsTop;
	}
	else
	{
		HUDPackage.CrosshairsCenter = nullptr;
		HUDPackage.CrosshairsLeft = nullptr;
		HUDPackage.CrosshairsRight = nullptr;
		HUDPackage.CrosshairsBottom = nullptr;
		HUDPackage.CrosshairsTop = nullptr;
	}
	// 计算准心散布

	FVector2D WalkSpeedRange(0.f, Character->GetCharacterMovement()->MaxWalkSpeed);
	FVector2D VelocityMultiplierRange(0.f, 1.f);
	FVector Velocity = Character->GetVelocity();
	Velocity.Z = 0.f;

	CrosshairVelocityFactor = FMath::GetMappedRangeValueClamped(WalkSpeedRange, VelocityMultiplierRange,
	                                                            Velocity.Size());

	if (Character->GetCharacterMovement()->IsFalling())
	{
		CrosshairInAirFactor = FMath::FInterpTo(CrosshairInAirFactor, 2.25f, DeltaTime, 2.25f);
	}
	else
	{
		CrosshairInAirFactor = FMath::FInterpTo(CrosshairInAirFactor, 0.f, DeltaTime, 30.f);
	}

	if (bAiming)
	{
		CrosshairAimFactor = FMath::FInterpTo(CrosshairAimFactor, 0.58f, DeltaTime, 30.f);
	}
	else
	{
		CrosshairAimFactor = FMath::FInterpTo(CrosshairAimFactor, 0.f, DeltaTime, 30.f);
	}

	// Show every new shot for at least one HUD frame. A long frame must not
	// consume the pulse before it can render; exponential decay is frame-rate independent.
	if (!bFreshCrosshairShot)
		CrosshairShootingFactor *= FMath::Exp(-8.f * FMath::Max(0.f, DeltaTime));
	bFreshCrosshairShot = false;
	HUDPackage.ShotPulse = CrosshairShootingFactor;

	HUDPackage.CrosshairSpread =
		(Character->bIsCrouched ? .35f : .5f) +
		CrosshairVelocityFactor +
		CrosshairInAirFactor -
		CrosshairAimFactor +
		CrosshairShootingFactor;

	HUD->SetHUDPackage(HUDPackage);
}

void UCombatComponent::UpdateCarriedAmmo()
{
	if (EquippedWeapon == nullptr) return;
	if (CarriedAmmoMap.Contains(EquippedWeapon->GetWeaponType()))
	{
		CarriedAmmo = CarriedAmmoMap[EquippedWeapon->GetWeaponType()];
	}

	Controller = Controller == nullptr ? Cast<ABrokenEdgePlayerController>(Character->Controller) : Controller;
	if (Controller)
	{
		Controller->SetHUDCarriedAmmo(CarriedAmmo);
	}
}

void UCombatComponent::HandleReload()
{
	if (Character)
	{
		Character->PlayReloadMontage();
	}
}


void UCombatComponent::ServerSetAiming_Implementation(bool bIsAiming)
{
	if (bIsAiming && Character && Character->IsTraversing()) return;
	bAiming = bIsAiming;
	if (Character)
	{
		Character->GetCharacterMovement()->MaxWalkSpeed = bIsAiming ? AimWalkSpeed : BaseWalkSpeed;
	}
}

void UCombatComponent::OnRep_EquippedWeapon()
{
	//@TODO


	if (EquippedWeapon && Character)
	{
		//EquippedWeapon->SetSyncAmmo();
		// EquippedWeapon->SetSquence();
		EquippedWeapon->SetWeaponState(EWeaponState::EWS_Equipped);
		AttachActorToRightHand(EquippedWeapon);
		Character->GetCharacterMovement()->bOrientRotationToMovement = false;
		Character->bUseControllerRotationYaw = true;
		// PlayEquipWeaponSound(EquippedWeapon);
		EquippedWeapon->EnableCustomDepth(false);
		EquippedWeapon->SetHUDAmmo();
	}
	if (Character) Character->RefreshFirstPersonWorldWeapons();
}

void UCombatComponent::OnRep_SecondaryWeapon()
{
	if (SecondaryWeapon && Character)
	{
		SecondaryWeapon->SetWeaponState(EWeaponState::EWS_EquippedSecondary);
		AttachActorToBackpack(SecondaryWeapon);
		// PlayEquipWeaponSound(EquippedWeapon);//@TODO
	}
	if (Character) Character->RefreshFirstPersonWorldWeapons();
}

void UCombatComponent::OnRep_Aiming()
{
	if (Character && Character->IsLocallyControlled())
	{
		bAiming = bAimButtonPressed;
	}
}

void UCombatComponent::InterpFOV(float DeltaTime)
{
	if (EquippedWeapon == nullptr) return;
	if (Character && Character->GetPresentation().FirstPersonMeshAsset && Character->IsFirstPersonView())
	{
		const float AimFOV = EquippedWeapon->Presentation && EquippedWeapon->Presentation->ScopeOverlay
			                     ? EquippedWeapon->Presentation->ScopeFOV
			                     : Character->FirstPersonAimFOV;
		const float Target = bAiming ? AimFOV : DefaultFOV;
		CurrentFOV = Character->GetExtractionCombat()->IsEnabled()
			             ? FMath::Lerp(DefaultFOV, AimFOV,
			                           Character->GetExtractionCombat()->GetAimAlpha())
			             : FMath::FInterpTo(CurrentFOV, Target, DeltaTime, Character->FirstPersonAimInterpSpeed);
		Character->GetFollowCamera()->SetFieldOfView(CurrentFOV);
		return;
	}

	if (bAiming)
	{
		CurrentFOV = FMath::FInterpTo(CurrentFOV, EquippedWeapon->ZoomedFOV, DeltaTime,
		                              EquippedWeapon->ZoomInterpSpeed);
	}
	else
	{
		CurrentFOV = FMath::FInterpTo(CurrentFOV, DefaultFOV, DeltaTime, ZoomInterpSpeed);
	}

	if (Character && Character->GetFollowCamera())
	{
		Character->GetFollowCamera()->SetFieldOfView(CurrentFOV);
	}
}

void UCombatComponent::OnRep_HoldingTheFlag()
{
	//@TODO
}

void UCombatComponent::OnRep_CombatState()
{
    // Delayed legacy replication must not drive extraction presentation.
    if (!Character || Character->GetExtractionCombat()->IsEnabled()) return;
    if (CombatState == ECombatState::ECS_Reloading && !Character->IsLocallyControlled()) HandleReload();
    // Authority may replicate Idle before the predicted montage reaches its end notify.
    if (CombatState == ECombatState::ECS_Unoccuiped) Character->bFinishedSwapping = true;
    else if (CombatState == ECombatState::ECS_SwappingWeapon) Character->bFinishedSwapping = false;
}

void UCombatComponent::OnRep_Flag()
{
	//@TODO
}

void UCombatComponent::OnRep_CarriedAmmo()
{
	Controller = Controller == nullptr ? Cast<ABrokenEdgePlayerController>(Character->Controller) : Controller;

	if (Controller)
	{
		Controller->SetHUDCarriedAmmo(CarriedAmmo);
	}

	//@TODO
	// bool bJumpToShotgunEnd =
	// CombatState == ECombatState::ECS_Reloading &&
	// EquippedWeapon != nullptr &&
	// EquippedWeapon->GetWeaponType() == EWeaponType::EWT_Shotgun &&
	// CarriedAmmo == 0;
	// if (bJumpToShotgunEnd)
	// {
	// 	JumpToShotgunEnd();
	// }
}

void UCombatComponent::InitializeCarriedAmmo()
{
	CarriedAmmoMap.Emplace(EWeaponType::EWT_AssaultRifle, StartingARAmmo);
	CarriedAmmoMap.Emplace(EWeaponType::EWT_RocketLauncher, StartingRocketAmmo);
	CarriedAmmoMap.Emplace(EWeaponType::EWT_Pistol, StartingPistolAmmo);
	CarriedAmmoMap.Emplace(EWeaponType::EWT_SMG, StartingSMGAmmo);
	CarriedAmmoMap.Emplace(EWeaponType::EWT_Shotgun, StartingShotgunAmmo);
	CarriedAmmoMap.Emplace(EWeaponType::EWT_SniperRifle, StartingSniperAmmo);
	CarriedAmmoMap.Emplace(EWeaponType::EWT_GrenadeLauncher, StartingGrenadeAmmo);
}

bool UCombatComponent::ShouldSwapWeapons()
{
	// UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s %s"),
	// 	GFrameCounter, TEXT(__FUNCTION__), __LINE__,
	// 	EquippedWeapon != nullptr?TEXT("有主武器"):TEXT("没有主武器"),
	// 	SecondaryWeapon != nullptr?TEXT("有副武器"):TEXT("没有副武器"));

	return (EquippedWeapon != nullptr && SecondaryWeapon != nullptr);
}

void UCombatComponent::SyncReserveFromInventory(int32 Ammo)
{
    CarriedAmmo = FMath::Max(0, Ammo);
    CarriedAmmoMap.FindOrAdd(EWeaponType::EWT_AssaultRifle) = CarriedAmmo;
    UpdateCarriedAmmo();
}
