#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ExtractionWeaponVisuals.generated.h"

UCLASS()
class BROKENEDGE_API UExtractionWeaponVisuals : public UActorComponent
{
	GENERATED_BODY()
public:
	UExtractionWeaponVisuals();
	virtual void TickComponent(float Delta, ELevelTick Kind, FActorComponentTickFunction* Tick) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	TArray<FName> Built;
	UPROPERTY(Transient) TArray<TObjectPtr<class UStaticMeshComponent>> WorldParts;
	UPROPERTY(Transient) TArray<TObjectPtr<class UStaticMeshComponent>> ViewParts;
	TWeakObjectPtr<class USkeletalMeshComponent> ViewParent;
	void Clear();
};
