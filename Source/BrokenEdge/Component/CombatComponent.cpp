// Fill out your copyright notice in the Description page of Project Settings.


#include "CombatComponent.h"

#include "BrokenEdge/BrokenEdgeTypes/WeaponType.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "BrokenEdge/Weapon/Weapon.h"
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
	if (CombatState != ECombatState::ECS_Unoccuiped || Character == nullptr || !Character->HasAuthority()) return;

	Character->PlaySwapMontage();
	CombatState = ECombatState::ECS_SwappingWeapon;
	Character->bFinishedSwapping = false;
	if (SecondaryWeapon) SecondaryWeapon->EnableCustomDepth(false);
}


void UCombatComponent::FinishSwap()
{
	if (Character && Character->HasAuthority())
	{
		CombatState = ECombatState::ECS_Unoccuiped;
	}

	if (Character) Character->bFinishedSwapping = true;

	if (SecondaryWeapon) SecondaryWeapon->EnableCustomDepth(true);
}

void UCombatComponent::FinishSwapAttachWeapons()
{
	if (Character == nullptr || !Character->HasAuthority()) return;
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
}


/**
 * @brief 换弹逻辑
 */
void UCombatComponent::Reload()
{
	if (CarriedAmmo > 0 && CombatState == ECombatState::ECS_Unoccuiped && 
		!EquippedWeapon->IsFull() && !bLocallyReloading)
	{
		ServerReload();
		HandleReload();
		bLocallyReloading = true;
	}
}


void UCombatComponent::ServerReload_Implementation()
{
	if (Character == nullptr || EquippedWeapon == nullptr)return;
	
	CombatState = ECombatState::ECS_Reloading;
	
	if (!Character->IsLocallyReloading())HandleReload();
	
}

// Called when the game starts
void UCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	if (Character)
	{
		Character->GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;

		//@TODO
		// if (Character->GetFollowCamera())
		// {
		// 	DefaultFOV = Character->GetFollowCamera()->FieldOfView;
		// 	CurrentFOV = DefaultFOV;
		// }

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
		// InterpFOV(DeltaTime);
	}
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
	if (const USkeletalMeshSocket* HandSocket = Character->GetMesh()->GetSocketByName(FName("RightHandSocket")))
	{
		HandSocket->AttachActor(ActorToAttach, Character->GetMesh());
	}
}

void UCombatComponent::AttachActorToBackpack(AActor* ActorToAttach)
{
	if (Character == nullptr || Character->GetMesh() == nullptr || ActorToAttach == nullptr) return;
	if (const USkeletalMeshSocket* BackpackSocket = Character->GetMesh()->GetSocketByName(FName("BackpackSocket")))
	{
		BackpackSocket->AttachActor(ActorToAttach, Character->GetMesh());
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
	EquippedWeapon->SetHUDAmmo();
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
}

void UCombatComponent::SetAiming(bool bIsAiming)
{
	if (Character == nullptr || EquippedWeapon == nullptr) return;
	bAiming = bIsAiming;
	ServerSetAiming(bIsAiming);
	if (Character)
	{
		Character->GetCharacterMovement()->MaxWalkSpeed = bIsAiming ? AimWalkSpeed : BaseWalkSpeed;
	}
	//@TODO
	// if (Character->IsLocallyControlled() && EquippedWeapon->GetWeaponType() == EWeaponType::EWT_SniperRifle)
	// {
	// 	Character->ShowSniperScopeWidget(bIsAiming);
	// }
	if (Character->IsLocallyControlled()) bAimButtonPressed = bIsAiming;
}

void UCombatComponent::TraceUnderCrosshairs(FHitResult& TraceHitResult)
{
	FVector2D ViewportSize;
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewportSize);
	}

	FVector2D CrosshairLocation(ViewportSize.X / 2.f, ViewportSize.Y / 2.f);

	FVector CrosshairWorldPosition;
	FVector CrosshairWorldDirection;
	bool bScreenToWorld = UGameplayStatics::DeprojectScreenToWorld(
		UGameplayStatics::GetPlayerController(this, 0),
		CrosshairLocation,
		CrosshairWorldPosition,
		CrosshairWorldDirection
	);

	if (bScreenToWorld)
	{
		FVector Start = CrosshairWorldPosition;

		if (Character)
		{
			float DistanceToCharacter = (Character->GetActorLocation() - Start).Size();
			Start += CrosshairWorldDirection * (DistanceToCharacter + 100.f);
		}

		FVector End = Start + CrosshairWorldDirection * TRACE_LENGTH;

		bool bHIt = GetWorld()->LineTraceSingleByChannel(
			TraceHitResult,
			Start,
			End,
			ECollisionChannel::ECC_Visibility
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

	if (EquippedWeapon)
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

	CrosshairShootingFactor = FMath::FInterpTo(CrosshairShootingFactor, 0.f, DeltaTime, 40.f);

	HUDPackage.CrosshairSpread =
		0.5f +
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
}

void UCombatComponent::OnRep_SecondaryWeapon()
{
	//@TODO
}

void UCombatComponent::OnRep_Aiming()
{
	if (Character && Character->IsLocallyControlled())
	{
		bAiming = bAimButtonPressed;
	}
}

void UCombatComponent::OnRep_HoldingTheFlag()
{
	//@TODO
}

void UCombatComponent::OnRep_CombatState()
{
	//@TODO
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
