// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Projectile.generated.h"

UCLASS()
class BROKENEDGE_API AProjectile : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AProjectile();

	/** 是否使用服务器倒带 */
	bool bUseServerSideRewind = false;
	
	/** 基础伤害 */
	UPROPERTY(EditAnywhere)
	float Damage = 20.f;

	/** 爆头伤害 */
	UPROPERTY(EditAnywhere)
	float HeadShotDamage = 40.f;
	
	/** 射弹初始速度 */
	UPROPERTY(EditAnywhere)
	float InitialSpeed = 15000;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	/**
 * 碰撞命中回调
 * @param HitComp 碰撞组件
 * @param OtherActor 命中的 Actor
 * @param OtherComp 命中的组件
 * @param NormalImpulse 法向冲量
 * @param Hit 命中结果
 */
	UFUNCTION()
	virtual void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, 
		UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	/** 碰撞检测组件 */
	UPROPERTY(EditAnywhere)
	class UBoxComponent* CollisionBox;

	/** 射弹静态网格体组件 */
	UPROPERTY(VisibleAnywhere)
	UStaticMeshComponent* ProjectileMesh;
	
	/** 射弹移动组件 */
	UPROPERTY(VisibleAnywhere)
	class UProjectileMovementComponent* ProjectileMovementComponent;

	/** 造成径向爆炸伤害 */
	void ExplodeDamage();

	/** 爆炸伤害内半径 */
	UPROPERTY(EditAnywhere)
	float DamageInnerRadius = 200.f;

	/** 爆炸伤害外半径 */
	UPROPERTY(EditAnywhere)
	float DamageOutRadius = 500.f;

	/** 启动销毁计时器 */
	void StartDestroyTimer();

	/** 延迟销毁时间（秒） */
	UPROPERTY(EditAnywhere)
	float DestroyTime = 3.f;

	/** 销毁计时器结束回调 */
	void DestroyTimerFinished();

	/** 销毁计时器句柄 */
	FTimerHandle DestroyTimer;

	/** 命中粒子特效 */
	UPROPERTY(EditAnywhere)
	class UParticleSystem* ImpactParticles;

	/** 命中音效 */
	UPROPERTY(EditAnywhere)
	class USoundCue* ImpactSound;

	/** Niagara 轨迹特效 */
	UPROPERTY(EditAnywhere)
	class UNiagaraSystem* TrailSystem;

	/** 运行时生成的 Niagara 轨迹组件 */
	UPROPERTY()
	class UNiagaraComponent* TrailSystemComponent;
	
public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	
	
};
