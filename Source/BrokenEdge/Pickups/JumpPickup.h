// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Pickup.h"
#include "JumpPickup.generated.h"

/**
 *
 */
UCLASS()
class BROKENEDGE_API AJumpPickup : public APickup
{
	GENERATED_BODY()

protected:
	// virtual void BeginPlay() override;

	virtual void OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	                             UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult) override;

private:
	/** 跳跃Z轴速度加成 */
	UPROPERTY(EditAnywhere)
	float JumpZVelocityBuff = 4000.f;

	/** 跳跃加成持续时间 */
	UPROPERTY(EditAnywhere)
	float JumpBuffTime = 30.f;


};
