// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Pickup.h"
#include "HealthPickup.generated.h"

/**
 * 
 */
UCLASS()
class BROKENEDGE_API AHealthPickup : public APickup
{
	GENERATED_BODY()
	
public:
	/** 构造函数 */
	// AHealthPickup();

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
	virtual void OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, 
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)override;

private:
	/** 恢复的生命值 */
	UPROPERTY(EditAnywhere)
	float HealthAmount = 100.f;

	/** 恢复时间 */
	UPROPERTY(EditAnywhere)
	float HealthingTime = 5.f;
	
	
};
