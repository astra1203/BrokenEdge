#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "ExtractionTraversalTypes.h"
#include "ExtractionTraversalComponent.generated.h"

class UAnimSequence;
class UAnimMontage;

USTRUCT(BlueprintType)
struct FTraversalAnimation
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	EExtractionTraversal Kind = EExtractionTraversal::Vault;
	UPROPERTY(EditAnywhere)
	float Height = 120;
	UPROPERTY(EditAnywhere)
	bool bTop = false;
	UPROPERTY(EditAnywhere)
	TObjectPtr<UAnimSequence> FirstPerson;
	UPROPERTY(EditAnywhere)
	TObjectPtr<UAnimSequence> ThirdPerson;
};

UCLASS(BlueprintType)
class BROKENEDGE_API UTraversalAnimationSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	TArray<FTraversalAnimation> Animations;
};

/** Replicated visual state; capsule motion is exclusively owned by CMC. */
UCLASS(ClassGroup=Movement, meta=(BlueprintSpawnableComponent))
class BROKENEDGE_API UExtractionTraversalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UExtractionTraversalComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(EditAnywhere)
	TSoftObjectPtr<UTraversalAnimationSet> AnimationSet;
	UPROPERTY(Replicated)
	FExtractionTraversalState VisualState;
	void Publish(const FExtractionTraversalState& State);
	bool Active() const;
	FString Prompt() const;

private:
	UPROPERTY(Transient)
	TObjectPtr<UTraversalAnimationSet> LoadedAnimations;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ViewMontage;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> BodyMontage;
	UPROPERTY(Transient)
	TObjectPtr<class AWeapon> StowedWeapon;
	uint16 LastSerial = MAX_uint16;
	bool bWasActive = false, bWeaponWasVisible = true;
	bool bWeaponWasHidden = false;
	// 本次表现是否切到了“看完整身体”的爬梯视角，结束时据此还原。
	bool bLadderBodyView = false;
	float RemoteElapsed = 0;
	float LastRepElapsed = -1;
	void StopVisuals();
};
