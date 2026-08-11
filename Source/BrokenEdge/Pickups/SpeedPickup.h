// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Pickup.h"
#include "SpeedPickup.generated.h"

/**
 *
 */
UCLASS()
class BROKENEDGE_API ASpeedPickup : public APickup
{
	GENERATED_BODY()


protected:
	/**
	 * 球体重叠回调（重写）
	 * @param OverlappedComponent 重叠的组件
	 * @param OtherActor 重叠的Actor
	 * @param OtherComp 重叠的组件
	 * @param OtherBodyIndex 身体索引
	 * @param bFromSweep 是否来自扫描
	 * @param SweepResult 扫描结果
	 */
	virtual void OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

private:
	/** 基础移动速度加成 */
	UPROPERTY(EditAnywhere)
	float BaseSpeedBuff = 1600.f;

	/** 蹲下移动速度加成 */
	UPROPERTY(EditAnywhere)
	float CrouchSpeedBuff = 850.f;

	/** 速度加成持续时间 */
	UPROPERTY(EditAnywhere)
	float SpeedBuffTime = 30.f;


};
