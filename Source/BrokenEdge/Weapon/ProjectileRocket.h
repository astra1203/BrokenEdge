// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Projectile.h"
#include "ProjectileRocket.generated.h"

/**
 *
 */
UCLASS()
class BROKENEDGE_API AProjectileRocket : public AProjectile
{
	GENERATED_BODY()
public:
	/** 构造函数 */
	AProjectileRocket();

protected:
	/**
 * 碰撞命中回调（重写）
 * @param HitComp 碰撞组件
 * @param OtherActor 命中的Actor
 * @param OtherComp 命中的组件
 * @param NormalImpulse 法向冲量
 * @param Hit 命中结果
 */
	virtual void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp,  FVector NormalImpulse, const FHitResult& Hit) override;



	/** 火箭移动组件 */
	UPROPERTY(VisibleAnywhere)
	class URocketMovementComponent* RocketMovementComponent;
};
