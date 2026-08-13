// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon.h"

#include "BrokenEdge/BrokenEdge.h"
#include "BrokenEdge/BrokenEdgeTypes/WeaponType.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "Components/SphereComponent.h"
#include "Components/WidgetComponent.h"
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

void AWeapon::SetHUDAmmo()
{
	//@TODO
}

// Called when the game starts or when spawned
void AWeapon::BeginPlay()
{
	Super::BeginPlay();
	
	AreaSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	AreaSphere->SetCollisionResponseToChannel(ECollisionChannel::ECC_Pawn, ECollisionResponse::ECR_Overlap);
	
	AreaSphere->OnComponentBeginOverlap.AddDynamic(this, &AWeapon::OnSphereOverlap);
	
	AreaSphere->OnComponentEndOverlap.AddDynamic(this, &AWeapon::OnSphereEndOverlap);

	if (PickUpWidget)
	{
		PickUpWidget->SetVisibility(false);
	}
}

// Called every frame
void AWeapon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void AWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWeapon, WeaponState);

	//@TODO
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
void AWeapon::OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT(""));
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
	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT(""));
	ABrokenEdgeCharacter* BrokenEdgeCharacter = Cast<ABrokenEdgeCharacter>(OtherActor);
	if (BrokenEdgeCharacter)
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

void AWeapon::Dropped()
{
	SetWeaponState(EWeaponState::EWS_Dropped);
	FDetachmentTransformRules DetachRules(EDetachmentRule::KeepWorld, true);
	WeaponMesh->DetachFromComponent(DetachRules);
	SetOwner(nullptr);

	//@TODO

}

void AWeapon::OnRep_WeaponState()
{
	//@TODO
	// OnWeaponStateSet()
}


void AWeapon::SetWeaponState(EWeaponState State)
{
	WeaponState = State;

	// OnWeaponStateSet();TODO
}
