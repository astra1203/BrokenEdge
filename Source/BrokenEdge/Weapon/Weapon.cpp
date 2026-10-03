// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon.h"

#include "VectorTypes.h"
#include "BrokenEdge/BrokenEdge.h"
#include "BrokenEdge/BrokenEdgeTypes/WeaponType.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "Components/SphereComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Kismet/KismetMathLibrary.h"
#include "Math/UnitConversion.h"
#include "Net/UnrealNetwork.h"


// Sets default values
AWeapon::AWeapon()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);

	WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	SetRootComponent(WeaponMesh);

	WeaponMesh->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Block);
	WeaponMesh->SetCollisionResponseToChannel(ECollisionChannel::ECC_Pawn, ECollisionResponse::ECR_Ignore);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	WeaponMesh->SetCustomDepthStencilValue(CUSTOM_DEPTH_BLUE);
	WeaponMesh->MarkRenderStateDirty();
	EnableCustomDepth(true);

	AreaSphere = CreateDefaultSubobject<USphereComponent>(TEXT("AreaSphere"));
	AreaSphere->SetupAttachment(RootComponent);
	AreaSphere->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
	AreaSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AreaSphere->SetCollisionResponseToChannel(ECC_SkeletalMesh, ECollisionResponse::ECR_Overlap);

	PickUpWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("PickUpWidget"));
	PickUpWidget->SetupAttachment(RootComponent);
}

/**
 * @brief 启用自定义深度
 * @param bEnable 是否启用
 */
void AWeapon::EnableCustomDepth(bool bEnable)
{
	if (WeaponMesh)
	{
		WeaponMesh->SetRenderCustomDepth(bEnable);
	}
}

void AWeapon::MulticastAmmo_Implementation(int32 UpdateAmmo)
{
	Ammo = UpdateAmmo;
}

void AWeapon::OnRep_ExtractionInitialAmmo()
{
	if (ExtractionInitialAmmo >= 0) SetExtractionAmmo(ExtractionInitialAmmo);
}

void AWeapon::SetHUDAmmo()
{
	BrokenEdgeOwnerCharacter = BrokenEdgeOwnerCharacter == nullptr
		                           ? Cast<ABrokenEdgeCharacter>(GetOwner())
		                           : BrokenEdgeOwnerCharacter;

	if (BrokenEdgeOwnerCharacter)
	{
		BrokenEdgeOwnerController = BrokenEdgeOwnerController == nullptr
			                            ? Cast<ABrokenEdgePlayerController>(BrokenEdgeOwnerCharacter->Controller)
			                            : BrokenEdgeOwnerController;

		if (BrokenEdgeOwnerController)
		{
			BrokenEdgeOwnerController->SetHUDWeaponAmmo(Ammo);
		}
	}
}

// Called when the game starts or when spawned
void AWeapon::BeginPlay()
{
	Super::BeginPlay();

	AreaSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	AreaSphere->SetCollisionResponseToChannel(ECollisionChannel::ECC_Pawn, ECollisionResponse::ECR_Overlap);

	if (HasAuthority())
	{
		AreaSphere->OnComponentBeginOverlap.AddDynamic(this, &AWeapon::OnSphereBeginOverlap);

		AreaSphere->OnComponentEndOverlap.AddDynamic(this, &AWeapon::OnSphereEndOverlap);
	}

	if (PickUpWidget)
	{
		PickUpWidget->SetVisibility(false);
	}
}

// Called every frame
void AWeapon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	RefreshOwnerViewVisibility();
}

void AWeapon::SetOwner(AActor* NewOwner)
{
	Super::SetOwner(NewOwner);
	RefreshOwnerViewVisibility();
}

void AWeapon::RefreshOwnerViewVisibility()
{
	if (!WeaponMesh) return;
	auto Character = Cast<ABrokenEdgeCharacter>(GetOwner());
	const bool bHideForOwner = IsValid(Character) && !Character->IsActorBeingDestroyed()
		&& Character->IsLocallyControlled() && Character->IsFirstPersonView()
		&& WeaponType != EWeaponType::EWT_Flag
		&& (WeaponState == EWeaponState::EWS_Equipped || WeaponState == EWeaponState::EWS_EquippedSecondary);
	if (!bHideForOwner)
	{
		RestoreOwnerViewVisibility();
		return;
	}
	if (!bOwnerViewVisibilityOverridden)
	{
		bSavedOwnerNoSee = WeaponMesh->bOwnerNoSee;
		SavedPrimitiveType = WeaponMesh->FirstPersonPrimitiveType;
		bOwnerViewVisibilityOverridden = true;
	}
	if (VisibilityOwner != Character)
	{
		VisibilityOwner = Character;
		Character->TrackFirstPersonWorldWeapon(this);
	}
	if (!WeaponMesh->bOwnerNoSee) WeaponMesh->SetOwnerNoSee(true);
	if (WeaponMesh->FirstPersonPrimitiveType != EFirstPersonPrimitiveType::WorldSpaceRepresentation)
		WeaponMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
}

void AWeapon::RestoreOwnerViewVisibility()
{
	if (bOwnerViewVisibilityOverridden && WeaponMesh)
	{
		WeaponMesh->SetOwnerNoSee(bSavedOwnerNoSee);
		WeaponMesh->SetFirstPersonPrimitiveType(SavedPrimitiveType);
	}
	bOwnerViewVisibilityOverridden = false;
	VisibilityOwner.Reset();
}

void AWeapon::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreOwnerViewVisibility();
	Super::EndPlay(EndPlayReason);
}

void AWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWeapon, WeaponState);
	DOREPLIFETIME_CONDITION(AWeapon, bUseServerSideRewind, COND_OwnerOnly);
	DOREPLIFETIME(AWeapon, ExtractionItemId);
	DOREPLIFETIME_CONDITION(AWeapon, ExtractionInitialAmmo, COND_InitialOnly);
}

void AWeapon::OnRep_Owner()
{
	// if (HasAuthority())
	// {
	// 	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"),
	// 		GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT("1"));
	// }
	// else
	// {
	// 	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"),
	// 		GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT("2"));
	// }
	Super::OnRep_Owner();
	RefreshOwnerViewVisibility();
	if (Owner == nullptr)
	{
		BrokenEdgeOwnerCharacter = nullptr;
		BrokenEdgeOwnerController = nullptr;
	}
	else
	{
		BrokenEdgeOwnerCharacter = BrokenEdgeOwnerCharacter == nullptr
			                           ? Cast<ABrokenEdgeCharacter>(Owner)
			                           : BrokenEdgeOwnerCharacter;

		if (BrokenEdgeOwnerCharacter &&
			BrokenEdgeOwnerCharacter->GetEquippedWeapon() &&
			BrokenEdgeOwnerCharacter->GetEquippedWeapon() == this)
		{
			SetHUDAmmo();
		}
	}
}

/**
 * @brief 球体重叠开始回调
 * @param OverlappedComponent 重叠组件
 * @param OtherActor 另一个Actor
 * @param OtherComp 另一个组件
 * @param OtherBodyIndex 组件索引
 * @param bFromSweep 是否来自扫描
 * @param SweepResult 扫描结果
 */
void AWeapon::OnSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
                                   UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
                                   const FHitResult& SweepResult)
{
	// UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT(""));
	ABrokenEdgeCharacter* BrokenEdgeCharacter = Cast<ABrokenEdgeCharacter>(OtherActor);
	if (BrokenEdgeCharacter)
	{
		//@TODO
		// if (WeaponType == EWeaponType::EWT_Flag && BrokenEdgeCharacter->GetTeam() == Team) return;
		// if (BrokenEdgeCharacter->IsHoldingTheFlag()) return;
		BrokenEdgeCharacter->SetOverlappingWeapon(this);
	}
}

void AWeapon::OnSphereEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
                                 UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	// UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT(""));
	ABrokenEdgeCharacter* BrokenEdgeCharacter = Cast<ABrokenEdgeCharacter>(OtherActor);
	// The character mesh and capsule can leave separately. Keep the pickup target
	// until every component of that character has left the sphere.
	if (BrokenEdgeCharacter && BrokenEdgeCharacter->GetOverlappingWeapon() == this &&
		!AreaSphere->IsOverlappingActor(BrokenEdgeCharacter))
	{
		//@TODO
		// if (WeaponType == EWeaponType::EWT_Flag && BrokenEdgeCharacter->GetTeam() == Team) return;
		// if (BrokenEdgeCharacter->IsHoldingTheFlag()) return;
		BrokenEdgeCharacter->SetOverlappingWeapon(nullptr);
	}
}

/**
 * @brief 显示/隐藏拾取Widget
 * @param bShowWidget 是否显示
 */
void AWeapon::ShowPickupWidget(bool bShowWidget)
{
	if (PickUpWidget)
	{
		PickUpWidget->SetVisibility(bShowWidget);
	}
}

void AWeapon::Fire(const FVector& HitTarget)
{
	if (FireAnimation)
	{
		WeaponMesh->PlayAnimation(FireAnimation, false);
	}

	//@TODO

	SpendRound();
}

void AWeapon::SpendRound()
{
	Ammo = FMath::Clamp(Ammo - 1, 0, MagCapacity);
	SetHUDAmmo();

	//@TODO
}

void AWeapon::Dropped()
{
	SetWeaponState(EWeaponState::EWS_Dropped);
	FDetachmentTransformRules DetachRules(EDetachmentRule::KeepWorld, true);
	WeaponMesh->DetachFromComponent(DetachRules);
	SetOwner(nullptr);

	BrokenEdgeOwnerCharacter = nullptr;
	BrokenEdgeOwnerController = nullptr;
}

void AWeapon::OnRep_WeaponState()
{
	//@TODO 回调函数没有触发
	OnWeaponStateSet();
}


void AWeapon::SetWeaponState(EWeaponState State)
{
	WeaponState = State;

	OnWeaponStateSet(); //TODO
}

/**
 * @brief 武器状态改变时的处理
 */
void AWeapon::OnWeaponStateSet()
{
	// Drop restores the mesh before physics; equip also handles state/owner replication in either order.
	RefreshOwnerViewVisibility();
	switch (WeaponState)
	{
	case EWeaponState::EWS_Equipped:
		OnEquipped();
		break;
	case EWeaponState::EWS_EquippedSecondary:
		OnEquippedSecondary();
		break;
	case EWeaponState::EWS_Dropped:
		OnDropped();
		break;
	}
}

void AWeapon::OnEquipped()
{
	ShowPickupWidget(false);
	AreaSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetSimulatePhysics(false);
	WeaponMesh->SetEnableGravity(false);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (WeaponType == EWeaponType::EWT_SMG)
	{
		WeaponMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		WeaponMesh->SetEnableGravity(true);
		WeaponMesh->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
	}
	EnableCustomDepth(false);

	BrokenEdgeOwnerCharacter = BrokenEdgeOwnerCharacter == nullptr
		                           ? Cast<ABrokenEdgeCharacter>(GetOwner())
		                           : BrokenEdgeOwnerCharacter;

	//@TODO
	// if (BrokenEdgeOwnerCharacter && bUseServerSideRewind)
	// {
	// 	BlasterOwnerController = BlasterOwnerController == nullptr ? Cast<ABlasterPlayerController>(BlasterOwnerCharacter->Controller) : BlasterOwnerController;
	// 	if (BlasterOwnerController && HasAuthority() && !BlasterOwnerController->HighPingDelegate.IsBound())
	// 	{
	// 		BlasterOwnerController->HighPingDelegate.AddDynamic(this, &AWeapon::OnPingTooHigh);
	// 	}
	// }
}

void AWeapon::OnEquippedSecondary()
{
	ShowPickupWidget(false);
	AreaSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetSimulatePhysics(false);
	WeaponMesh->SetEnableGravity(false);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (WeaponType == EWeaponType::EWT_SMG)
	{
		WeaponMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		WeaponMesh->SetEnableGravity(true);
		WeaponMesh->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
	}

	if (WeaponMesh)
	{
		WeaponMesh->SetCustomDepthStencilValue(CUSTOM_DEPTH_TAN);
		WeaponMesh->MarkRenderStateDirty();
	}

	//@TODO
	// BlasterOwnerCharacter = BlasterOwnerCharacter == nullptr ? Cast<ABlasterCharacter>(GetOwner()) : BlasterOwnerCharacter;
	// if (BlasterOwnerCharacter)
	// {
	// 	BlasterOwnerController = BlasterOwnerController == nullptr ? Cast<ABlasterPlayerController>(BlasterOwnerCharacter->Controller) : BlasterOwnerController;
	// 	if (BlasterOwnerController && HasAuthority() && BlasterOwnerController->HighPingDelegate.IsBound())
	// 	{
	// 		BlasterOwnerController->HighPingDelegate.RemoveDynamic(this, &AWeapon::OnPingTooHigh);
	// 	}
	// }
}

void AWeapon::OnDropped()
{
	if (HasAuthority())
	{
		AreaSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	WeaponMesh->SetSimulatePhysics(true);
	WeaponMesh->SetEnableGravity(true);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	WeaponMesh->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Block);
	WeaponMesh->SetCollisionResponseToChannel(ECollisionChannel::ECC_Pawn, ECollisionResponse::ECR_Ignore);
	WeaponMesh->SetCollisionResponseToChannel(ECollisionChannel::ECC_Camera, ECollisionResponse::ECR_Ignore);

	WeaponMesh->SetCustomDepthStencilValue(CUSTOM_DEPTH_BLUE);
	WeaponMesh->MarkRenderStateDirty();
	EnableCustomDepth(true);

	//@TODO
	// BlasterOwnerCharacter = BlasterOwnerCharacter == nullptr ? Cast<ABlasterCharacter>(GetOwner()) : BlasterOwnerCharacter;
	// if (BlasterOwnerCharacter)
	// {
	// 	BlasterOwnerController = BlasterOwnerController == nullptr ? Cast<ABlasterPlayerController>(BlasterOwnerCharacter->Controller) : BlasterOwnerController;
	// 	if (BlasterOwnerController && HasAuthority() && BlasterOwnerController->HighPingDelegate.IsBound())
	// 	{
	// 		BlasterOwnerController->HighPingDelegate.RemoveDynamic(this, &AWeapon::OnPingTooHigh);
	// 	}
	// }
}

bool AWeapon::IsFull() const
{
	if (Ammo > MagCapacity)
		UE_LOG(LogTemp, Warning, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
	       TEXT("当前弹药数大于弹匣最大容量。"));

	return Ammo == MagCapacity;
}

/**
 * @brief 添加弹药
 * @param AmmoToAdd 要添加的弹药数量
 */
void AWeapon::AddAmmo(int32 AmmoToAdd)
{
	Ammo = FMath::Clamp(Ammo + AmmoToAdd, 0, MagCapacity);
	SetHUDAmmo();
	ClientAddAmmo(AmmoToAdd);
}

bool AWeapon::IsEmpty()
{
	return Ammo <= 0;
}

FVector AWeapon::TraceEndWithScatter(const FVector& HitTarget)
{
	const USkeletalMeshSocket* MuzzleFlashSocket = GetWeaponMesh()->GetSocketByName("MuzzleFlash");
	if (MuzzleFlashSocket == nullptr)return FVector::ZeroVector;
	const FTransform SocketTransform = MuzzleFlashSocket->GetSocketTransform(GetWeaponMesh());
	const FVector TraceStart = SocketTransform.GetLocation();

	const FVector ToTargetNormalized = (HitTarget - TraceStart).GetSafeNormal();
	const FVector SphereCenter = TraceStart + ToTargetNormalized * DistanceToSphere;
	const FVector RandVec = UKismetMathLibrary::RandomUnitVector() * FMath::FRandRange(0.f, SphereRadius);
	const FVector EndLoc = SphereCenter + RandVec;

	const FVector ToEndLoc = EndLoc - TraceStart;

	return FVector(TraceStart + ToEndLoc * TRACE_LENGTH / ToEndLoc.Size());
}

/**
 * @brief 客户端添加弹药
 * @param AmmoToAdd 要添加的弹药数量
 */
void AWeapon::ClientAddAmmo_Implementation(int32 AmmoToAdd)
{
	if (HasAuthority()) return;

	Ammo = FMath::Clamp(Ammo + AmmoToAdd, 0, MagCapacity);
	BrokenEdgeOwnerCharacter = BrokenEdgeOwnerCharacter == nullptr
		                           ? Cast<ABrokenEdgeCharacter>(GetOwner())
		                           : BrokenEdgeOwnerCharacter;

	bool IsShotgunFull = BrokenEdgeOwnerCharacter
		&& BrokenEdgeOwnerCharacter->GetCombat()
		&& BrokenEdgeOwnerCharacter->GetEquippedWeapon()
		&& WeaponType == EWeaponType::EWT_Shotgun
		&& IsFull();

	//@TODO
	// if (IsShotgunFull)
	// {
	// 	// BrokenEdgeOwnerCharacter->GetCombat()->JumpToShotgunEnd();
	// }

	SetHUDAmmo();
}
