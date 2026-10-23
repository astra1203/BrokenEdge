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

private:
	void UpdateTrail();
	FResolvedWeaponStats Stats;
	FVector Velocity = FVector::ZeroVector;
	float Distance = 0, Accumulator = 0, BodyDamage = 0, HeadDamage = 0;
	FName AmmoType;
	bool bPredicted = false;
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
