// Fill out your copyright notice in the Description page of Project Settings.


#include "Projectile.h"

#include "BrokenEdge/BrokenEdge.h"
#include "Components/BoxComponent.h"
#include "Kismet/GameplayStatics.h"


// Sets default values
AProjectile::AProjectile()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
	SetRootComponent(CollisionBox);

	CollisionBox->SetCollisionObjectType(ECollisionChannel::ECC_WorldDynamic);
	CollisionBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionBox->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
	CollisionBox->SetCollisionResponseToChannel(ECollisionChannel::ECC_Visibility, ECollisionResponse::ECR_Block);
	CollisionBox->SetCollisionResponseToChannel(ECollisionChannel::ECC_WorldStatic, ECollisionResponse::ECR_Block);
	CollisionBox->SetCollisionResponseToChannel(ECC_SkeletalMesh, ECollisionResponse::ECR_Block);
}

// Called when the game starts or when spawned
void AProjectile::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		CollisionBox->OnComponentHit.AddDynamic(this, &AProjectile::OnHit);
	}
}

void AProjectile::OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
                        FVector NormalImpulse, const FHitResult& Hit)
{
	Destroy();
}

void AProjectile::ExplodeDamage()
{
	APawn* FireingPawn = GetInstigator();

	if (FireingPawn == nullptr || !HasAuthority()) return;

	AController* FireingController = FireingPawn->GetController();
	if (FireingController == nullptr) return;

	UGameplayStatics::ApplyRadialDamageWithFalloff(
		this, // 造成伤害的对象
		Damage, // 最大伤害
		10.f, // 最小伤害
		GetActorLocation(), // 爆炸伤害位置
		DamageInnerRadius, // 爆炸内半径
		DamageOutRadius, // 爆炸外半径
		1.f, // 伤害衰减
		UDamageType::StaticClass(), // 伤害类型
		TArray<AActor*>(), // 要忽略的对象数组
		this, // 伤害造成者
		FireingController // 伤害者控制器
	);

	// DrawDebugSphere(GetActorLocation(), DamageInnerRadius, 12, FColor::Red, true, 1.f);
	DrawDebugSphere(GetWorld(), GetActorLocation(), DamageInnerRadius, 12, FColor::Orange, true);

}

/**
 * @brief 生成轨迹特效
 */
// void AProjectile::SpawnTrailSystem()
// {
// 	if (TrailSystem)
// 	{
// 		TrailSystemComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(TrailSystem,
// 			GetRootComponent(),
// 			FName(),
// 			GetActorLocation(),
// 			GetActorRotation(),
// 			EAttachLocation::KeepWorldPosition,
// 			false);
// 	}
// }

void AProjectile::StartDestroyTimer()
{
	GetWorldTimerManager().SetTimer(
	DestroyTimer,
	this,
	&AProjectile::DestroyTimerFinished,
	DestroyTime);
}

void AProjectile::DestroyTimerFinished()
{
	Destroy();
}

// Called every frame
void AProjectile::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}
