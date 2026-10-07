#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ExtractionAbilities.generated.h"

class ABrokenEdgeCharacter;

UENUM()
enum class EExtractionAction : uint8 { None, Revive, Heal, Smoke };

/** Server clocks and tuning shared by gameplay, HUD and smoke visibility. */
UCLASS(Config=Game, DefaultConfig)
class BROKENEDGE_API UExtractionRules : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(Config)
	float BleedSeconds = 30;
	UPROPERTY(Config)
	float DownHealth = 100;
	UPROPERTY(Config)
	float CrawlSpeed = 60;
	UPROPERTY(Config)
	float ReviveSeconds = 5;
	UPROPERTY(Config)
	float ReviveRange = 200;
	UPROPERTY(Config)
	float ReviveHealth = 30;
	UPROPERTY(Config)
	float HealSeconds = 2;
	UPROPERTY(Config)
	float HealRange = 1000;
	UPROPERTY(Config)
	float HealAmount = 50;
	UPROPERTY(Config)
	float HealCooldown = 20;
	UPROPERTY(Config)
	float SmokeCastSeconds = .5f;
	UPROPERTY(Config)
	float SmokeRange = 1500;
	UPROPERTY(Config)
	float SmokeRadius = 400;
	UPROPERTY(Config)
	float SmokeSeconds = 15;
	UPROPERTY(Config)
	float SmokeCooldown = 30;
	UPROPERTY(Config)
	int32 Guards = 4;
	UPROPERTY(Config)
	float GuardReactionSeconds = 2.f;
	UPROPERTY(Config)
	float GuardShotInterval = .8f;
	UPROPERTY(Config)
	float GuardDamageScale = .3f;
};

UCLASS()
class BROKENEDGE_API UExtractionAbilities : public UActorComponent
{
	GENERATED_BODY()

public:
	UExtractionAbilities();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(Replicated)
	EExtractionAction Action = EExtractionAction::None;
	UPROPERTY(Replicated)
	double ActionEnd = 0;
	UPROPERTY(Replicated)
	TObjectPtr<ABrokenEdgeCharacter> Target;
	UPROPERTY(Replicated)
	double HealReadyAt = 0;
	UPROPERTY(Replicated)
	double SmokeReadyAt = 0;
	UPROPERTY(Replicated)
	TObjectPtr<ABrokenEdgeCharacter> Rescuer;
	void Request(EExtractionAction Kind, ABrokenEdgeCharacter* Other = nullptr);
	void ReleaseRescue();
	void Cancel();
	void Damaged();
	bool IsDowned() const;
	bool CanStart() const;
	ABrokenEdgeCharacter* RescueFocus() const;
	ABrokenEdgeCharacter* HealFocus() const;
	static bool Friendly(const ABrokenEdgeCharacter* A, const ABrokenEdgeCharacter* B);
	static bool Reach(const ABrokenEdgeCharacter* A, const ABrokenEdgeCharacter* B, float Range);
	UFUNCTION(Server, Reliable)
	void ServerBegin(uint32 Sequence, EExtractionAction Kind, ABrokenEdgeCharacter* Other);
	UFUNCTION(Server, Reliable)
	void ServerCancel(uint32 Sequence);

private:
	ABrokenEdgeCharacter* Character() const;
	uint32 LocalSequence = 0, LastSequence = 0;
	FVector StartedAt = FVector::ZeroVector;
	FVector SmokePoint = FVector::ZeroVector;
	bool ValidTarget() const;
	void Complete();
};
