// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Pickup.h"
#include "BrokenEdge/BrokenEdgeTypes/WeaponType.h"
#include "AmmoPickup.generated.h"

/**
 * 
 */
UCLASS()
class BROKENEDGE_API AAmmoPickup : public APickup
{
	GENERATED_BODY()
	
protected:
	virtual void OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, 
		AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, 
		bool bFromSweep, const FHitResult& SweepResult) override;

	
private:
	/** 弹药数量 */
	UPROPERTY(EditAnywhere)
	int32 AmmoAmount = 30;

	/** 适用的武器类型 */
	UPROPERTY(EditAnywhere)
	EWeaponType WeaponType;
	
};
