// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Pickup.generated.h"

/**
 * 拾取物基类，所有可拾取道具的基础类
 */
UCLASS()
class BROKENEDGE_API APickup : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	APickup();


protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UFUNCTION()
	virtual void OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** 基础旋转速度 */
	UPROPERTY(EditAnywhere)
	float BaseTurnRate = 45.f;

public:


	/** 拾取物被销毁时调用 */
	virtual void Destroyed() override;

private:
	/** 重叠球体组件 */
	UPROPERTY(EditAnywhere)
	class USphereComponent* OverlapSphere;

	/** 拾取物网格组件 */
	UPROPERTY(EditAnywhere)
	UStaticMeshComponent* PickupMesh;

	/** 拾取特效组件 */
	UPROPERTY(VisibleAnywhere)
	class UNiagaraComponent* PickupEffectComponent;
	
	/** 拾取特效组件 */
	// UPROPERTY(VisibleAnywhere)
	// class UNiagaraComponent* PickupEffectComponent;

	/** 绑定重叠计时器句柄 */
	FTimerHandle BindOverlapTimer;

	/** 绑定重叠延迟时间 */
	float BindOverlapTime = 0.25f;

	/** 绑定重叠计时器完成 */
	void BindOverlapTimerFinished();
};
