#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ExtractionMeleeComponent.generated.h"

class USkeletalMesh;

/** Permanent knife slot, independent of persisted firearm slots. Server owns all hit decisions. */
UCLASS()
class BROKENEDGE_API UExtractionMeleeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UExtractionMeleeComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(Replicated)
	bool Active = false;
	UPROPERTY(EditAnywhere)
	TObjectPtr<USkeletalMesh> Mesh;
	UPROPERTY(EditAnywhere)
	TObjectPtr<class UAnimSequence> Idle;
	UPROPERTY(EditAnywhere)
	TObjectPtr<class UAnimMontage> LightView;
	UPROPERTY(EditAnywhere)
	TObjectPtr<UAnimMontage> HeavyView;
	UPROPERTY(EditAnywhere)
	TObjectPtr<UAnimMontage> LightBody;
	UPROPERTY(EditAnywhere)
	TObjectPtr<UAnimMontage> HeavyBody;
	UPROPERTY(EditAnywhere)
	FTransform HandOffset;
	UFUNCTION(Server, Reliable)
	void Select(bool Knife, bool SwapGun = false);
	UFUNCTION(Server, Reliable)
	void Attack(bool Heavy);
	bool Busy() const;

private:
	class ABrokenEdgeCharacter* Character() const;
	bool CanAct() const;
	void Strike();
	UFUNCTION(NetMulticast, Reliable)
	void PlayAttack(bool Heavy);
	UPROPERTY(Transient)
	TObjectPtr<class USkeletalMeshComponent> WorldMesh;
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> ViewMesh;
	TWeakObjectPtr<class AWeapon> HiddenGun;
	FTimerHandle StrikeTimer;
	double NextAttack = 0;
	bool HeavyAttack = false;
};
