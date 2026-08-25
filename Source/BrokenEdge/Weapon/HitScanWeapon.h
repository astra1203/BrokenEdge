// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Weapon.h"
#include "HitScanWeapon.generated.h"

/**
 * 
 */
UCLASS()
class BROKENEDGE_API AHitScanWeapon : public AWeapon
{
	GENERATED_BODY()
	
public:
	/**
	 * 发射武器
	 * @param HitTarget 命中目标位置
	 */
	virtual void Fire(const FVector& HitTarget) override;
	
protected:
	/**
	 * 执行武器射线检测
	 * @param TraceStart 射线起点
	 * @param HitTarget 目标位置
	 * @param OutHit 命中结果输出
	 */
	void WeaponTraceHit(const FVector& TraceStart, const FVector& HitTarget, FHitResult& OutHit);

private:
	/** 光束粒子效果 */
	UPROPERTY(EditAnywhere)
	UParticleSystem* BeamParticles;

	
};
