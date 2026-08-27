// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Weapon.h"
#include "ProjectileWeapon.generated.h"

/**
 * 
 */
UCLASS()
class BROKENEDGE_API AProjectileWeapon : public AWeapon
{
	GENERATED_BODY()
	
public:
	/**
	 * 发射武器
	 * @param HitTarget 瞄准目标位置
	 */
	virtual void Fire(const FVector& HitTarget) override;
	
private:
	/** 射弹类 */
	UPROPERTY(EditAnywhere)
	TSubclassOf<class AProjectile> ProjectileClass;

};
