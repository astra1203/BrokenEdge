// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectileWeapon.h"

#include "Projectile.h"
#include "BrokenEdge/BrokenEdge.h"
#include "Engine/SkeletalMeshSocket.h"


void AProjectileWeapon::Fire(const FVector& HitTarget)
{
	Super::Fire(HitTarget);

	APawn* InstigatorPawn = Cast<APawn>(GetOwner());

	const USkeletalMeshSocket* MuzzleFlashSocket = GetWeaponMesh()->GetSocketByName(FName("MuzzleFlash"));

	UWorld* World = GetWorld();

	if (InstigatorPawn == nullptr || MuzzleFlashSocket == nullptr || World == nullptr) return;
	FTransform SocketTransform = MuzzleFlashSocket->GetSocketTransform(GetWeaponMesh());

	// 从枪口位置到目标位置的方向向量
	FVector ToTarget = HitTarget - SocketTransform.GetLocation();
	FRotator TargetRotation = ToTarget.Rotation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = InstigatorPawn;

	AProjectile* SpawnedProjectile = nullptr;
	if (bUseServerSideRewind) //使用服务器倒带
	{
		if (InstigatorPawn->HasAuthority()) //在服务器上
		{
			if (InstigatorPawn->IsLocallyControlled()) //聆听服务器模式下本地所控制的玩家（服务器端同时也是客户端所控制的玩家），生成真实的投射物
			{
				UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
				       *PrintNetMode(),TEXT("聆听服务器上生成子弹实体"));
				
				SpawnedProjectile = World->SpawnActor<AProjectile>(ProjectileClass,
				                                                   SocketTransform.GetLocation(), TargetRotation,
				                                                   SpawnParams);

				SpawnedProjectile->bUseServerSideRewind = false;
				SpawnedProjectile->Damage = Damage;
				SpawnedProjectile->HeadShotDamage = HeadShotDamage;
			}
			else //服务器上的远程玩家，生成用于倒带的投射物
			{
				UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
						*PrintNetMode(),TEXT("服务器上的远程玩家生成用于倒带的子弹实体"));

				SpawnedProjectile = World->SpawnActor<AProjectile>(ServerSideRewindProjectileClass,
				                                                   SocketTransform.GetLocation(), TargetRotation,
				                                                   SpawnParams);

				SpawnedProjectile->bUseServerSideRewind = true;
			}
		}
		else //在客户端
		{
			if (InstigatorPawn->IsLocallyControlled()) //本地所控制的玩家，生成用于倒带的投射物
			{
				UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
						*PrintNetMode(),TEXT("客户端本地所控制的玩家生成用于倒带的子弹实体"));
				
				SpawnedProjectile = World->SpawnActor<AProjectile>(ServerSideRewindProjectileClass,
				                                                   SocketTransform.GetLocation(), TargetRotation,
				                                                   SpawnParams);

				SpawnedProjectile->bUseServerSideRewind = true;
				
				SpawnedProjectile->TraceStart = SocketTransform.GetLocation();
				SpawnedProjectile->InitialVelocity = SpawnedProjectile->GetActorForwardVector() * SpawnedProjectile->InitialSpeed;

			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
					*PrintNetMode(),TEXT("客户端远程玩家生成不使用倒带的子弹实体"));
				
				SpawnedProjectile = World->SpawnActor<AProjectile>(ServerSideRewindProjectileClass,
				                                                   SocketTransform.GetLocation(), TargetRotation,
				                                                   SpawnParams);

				SpawnedProjectile->bUseServerSideRewind = false;
			}
		}
	}
	else //不使用服务器倒带
	{
		if (InstigatorPawn->HasAuthority())
		{
			AProjectile* SpawnProjectile = World->SpawnActor<AProjectile>(
				ProjectileClass, SocketTransform.GetLocation(),
				TargetRotation, SpawnParams);

			SpawnProjectile->bUseServerSideRewind = false;
			SpawnProjectile->Damage = Damage;
			SpawnProjectile->HeadShotDamage = HeadShotDamage;
		}
	}
}
