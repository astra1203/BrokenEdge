// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Pickup.h"
#include "ShieldPickup.generated.h"

/**
 * 
 */
UCLASS()
class BROKENEDGE_API AShieldPickup : public APickup
{
	GENERATED_BODY()
	
	
protected:
	/**
	 * @brief 球体重叠回调
	 */
	virtual void OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, 
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult) override;

private:
	/// 护盾恢复量
	UPROPERTY(EditAnywhere)
	float ShieldReplenishAmount = 100.f;

	/// 护盾恢复时间
	UPROPERTY(EditAnywhere)
	float ShieldReplenishTime = 5.f;
	
	
};
