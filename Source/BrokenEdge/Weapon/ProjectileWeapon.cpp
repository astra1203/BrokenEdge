// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectileWeapon.h"

#include "Projectile.h"
#include "Engine/SkeletalMeshSocket.h"


void AProjectileWeapon::Fire(const FVector& HitTarget)
{
	Super::Fire(HitTarget);

	APawn* InstigatorPawn = Cast<APawn>(GetOwner());

	const USkeletalMeshSocket* MuzzleFlashSocket = GetWeaponMesh()->GetSocketByName(FName("MuzzleFlash"));

	UWorld* World = GetWorld();
	
	if (InstigatorPawn == nullptr || MuzzleFlashSocket == nullptr||World==nullptr) return;
	FTransform SocketTransform = MuzzleFlashSocket->GetSocketTransform(GetWeaponMesh());
	
	// 从枪口位置到目标位置的方向向量
	FVector ToTarget = HitTarget - SocketTransform.GetLocation();
	FRotator TargetRotation = ToTarget.Rotation();
	
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = InstigatorPawn;
	
	if (bUseServerSideRewind)//使用服务器倒带
	{
		if (InstigatorPawn->HasAuthority())//在服务器上
		{
			
		}
		else//在客户端
		{
			
			
		}
	}
	else//不使用服务器倒带
	{
		if (InstigatorPawn->HasAuthority())
		{
			AProjectile* SpawnProjectile = World->SpawnActor<AProjectile>(ProjectileClass, SocketTransform.GetLocation(),
			                                                         TargetRotation ,SpawnParams);
			
			SpawnProjectile->bUseServerSideRewind=false;
			SpawnProjectile->Damage=Damage;
			SpawnProjectile->HeadShotDamage=HeadShotDamage;
		}
	}
	
	
}
