// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "HitScanWeapon.h"
#include "Shotgun.generated.h"

/**
 *
 */
UCLASS()
class BROKENEDGE_API AShotgun : public AHitScanWeapon
{
	GENERATED_BODY()

public:
	/**
 * 发射霰弹枪
 * @param HitTargets 命中目标位置数组
 */
	virtual void FireShotgun(const TArray<FVector_NetQuantize>& HitTargets);

	/**
	 * 霰弹枪散射计算
	 * @param HitTarget 原始瞄准目标
	 * @param HitTargets 输出的散射后目标位置数组
	 */
	void ShotgunTraceEndWithScatter(const FVector& HitTarget, TArray<FVector_NetQuantize>& HitTargets);



private:
	/** 每发射击的弹丸数量 */
	UPROPERTY(EditAnywhere, Category = "Weapon Scatter")
	uint32 NumberOfPellets = 10;
};
