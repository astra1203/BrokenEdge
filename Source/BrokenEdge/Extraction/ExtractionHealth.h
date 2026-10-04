#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ExtractionItems.h"
#include "ExtractionHealth.generated.h"

UCLASS(Config=Game, DefaultConfig)
class BROKENEDGE_API UExtractionEquipmentRules : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(Config)
	float TraumaThreshold = 15;
	UPROPERTY(Config)
	float BleedingInterval = 2;
	UPROPERTY(Config)
	float BleedingDamage = 1;
	UPROPERTY(Config)
	float FractureSpeedScale = .7f;
	UPROPERTY(Config)
	float StoppedAbsorption = .6f;
	UPROPERTY(Config)
	float PenetratedAbsorption = .2f;
};

/** Trauma belongs to the current pawn; durable equipment belongs to item instances. */
UCLASS()
class BROKENEDGE_API UExtractionHealth : public UActorComponent
{
	GENERATED_BODY()

public:
	UExtractionHealth();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void TickComponent(float Delta, ELevelTick Kind, FActorComponentTickFunction* Tick) override;
	UPROPERTY(Replicated)
	bool Bleeding = false;
	UPROPERTY(Replicated)
	bool LegFracture = false;
	void Bullet(float Damage, FName Bone, FName Ammo, AController* Attacker, AActor* Causer);
	bool Needs(EExtractionMedicalEffect Effect) const;
	bool Treat(const UExtractionItemDefinition* Definition);
	void Revived();
	static int32 ArmorSlotForBone(FName Bone);
	static bool IsLeg(FName Bone);
	static float Absorb(float Damage, int32 Penetration, int32 ArmorLevel, float& Durability);
#if WITH_DEV_AUTOMATION_TESTS
	int32 TestBulletSequence = 0;
	FName TestHitBone;
	float TestHealthLoss = 0;
#endif

private:
	double NextBleed = 0;
	TWeakObjectPtr<AController> BleedInstigator;
	class ABrokenEdgeCharacter* Character() const;
};
