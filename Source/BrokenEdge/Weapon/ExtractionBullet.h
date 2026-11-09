#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WeaponMechanics.h"
#include "ExtractionBullet.generated.h"

/** One server-owned flight = one damage event. No client hit RPC. */
UCLASS()
class BROKENEDGE_API AExtractionBullet : public AActor
{
	GENERATED_BODY()

public:
	AExtractionBullet();
	void Initialize(const FResolvedWeaponStats& Profile, const FVector& Direction, float Damage, float HeadDamage,
	                FName Ammo, class UParticleSystem* Impact, class USoundCue* Sound, class UParticleSystem* Trail);
	virtual void Tick(float DeltaSeconds) override;
	void SetVisualOrigin(const FVector& Origin) { PreviousVisual = Origin; }
	/**
	 * 服务器延迟补偿：大于 0 时，角色不再按当前姿态扫掠，而是按"本步时刻 - Seconds"的历史物理资产姿态检测，
	 * 即射手开枪时在屏幕上看到的位置。Seconds 已由服务器限制在倒带上限内。
	 */
	void SetRewind(float Seconds) { RewindSeconds = FMath::Max(0.f, Seconds); }
	float GetRewind() const { return RewindSeconds; }

private:
	void UpdateTrail();
	// 静态物体厚度不超过弹药穿透上限时，返回出射点并降低后续伤害；人物和厚墙都不穿透。
	bool TryPenetrate(const FHitResult& Hit, FVector& Exit);
	FResolvedWeaponStats Stats;
	FVector Velocity = FVector::ZeroVector;
	float Distance = 0, Accumulator = 0, BodyDamage = 0, HeadDamage = 0;
	FName AmmoType;
	bool bPredicted = false;
	// 穿透后保留的伤害比例；每发子弹最多穿透一次。
	float PenetrationScale = 1;
	float RewindSeconds = 0;
	bool bPenetrated = false;
	FVector PreviousVisual = FVector::ZeroVector;
	UPROPERTY(Replicated) TObjectPtr<class UParticleSystem> TrailEffect;
	UPROPERTY() TObjectPtr<class UParticleSystemComponent> TrailComponent;
	UPROPERTY(Replicated)
	TObjectPtr<class UParticleSystem> ImpactEffect;
	UPROPERTY(Replicated)
	TObjectPtr<class USoundCue> ImpactSound;
	UFUNCTION(NetMulticast, Reliable)
	void Impact(FVector_NetQuantize Point, FVector_NetQuantizeNormal Normal);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
