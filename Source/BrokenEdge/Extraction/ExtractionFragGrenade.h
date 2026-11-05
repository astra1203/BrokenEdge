#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionFragGrenade.generated.h"

class UProjectileMovementComponent;
class UStaticMeshComponent;
// 破片手雷：拔销即开始计时（延迟引信），落地弹跳不影响引信；到时在服务器结算爆炸伤害。
// 伤害照官方 DamageExplodeValue（ExplodeValueId 1）：满伤半径内 Damage，外圈按距离衰减到 MinDamage，
// 被墙体完全挡住的目标不受伤。表现由多播播放特效和音效。

UCLASS()
class BROKENEDGE_API AExtractionFragGrenade : public AActor
{
	GENERATED_BODY()

public:
	AExtractionFragGrenade();
	virtual void BeginPlay() override;
	// 服务器以给定初速抛出并开始引信计时。
	void Launch(const FVector& Velocity);

protected:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Shell;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Visual;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProjectileMovementComponent> Flight;
	UPROPERTY(EditDefaultsOnly, Category="Grenade")
	TObjectPtr<class UParticleSystem> ExplosionEffect;
	UPROPERTY(EditDefaultsOnly, Category="Grenade")
	TObjectPtr<class USoundBase> ExplosionSound;

private:
	void Explode();
	// 至少一条从爆点到目标身体（胸口或头部）的射线未被阻挡才算暴露。
	bool Exposed(const class ABrokenEdgeCharacter* Victim, const FVector& Origin) const;
	UFUNCTION(NetMulticast, Reliable)
	void MulticastExplode(FVector_NetQuantize Location);
	FTimerHandle FuseTimer;
	bool bExploded = false;
};
