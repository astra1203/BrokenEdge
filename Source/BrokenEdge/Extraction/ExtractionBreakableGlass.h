#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionBreakableGlass.generated.h"

/** Authored pane; match state lives in GameState so streamed panes cannot respawn intact. */
UCLASS()
class BROKENEDGE_API AExtractionBreakableGlass : public AActor
{
	GENERATED_BODY()

public:
	AExtractionBreakableGlass();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<class UStaticMeshComponent> Pane;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGuid PaneId;
	UPROPERTY(EditAnywhere)
	TObjectPtr<class USoundBase> BreakSound;
	UPROPERTY(EditAnywhere)
	TObjectPtr<class UParticleSystem> BreakEffect;
	bool IsBroken() const { return bBroken; }
	bool Break(const FVector& Impact, const FVector& Direction);
	void ApplyBroken();
	void PlayBreakEffect(const FVector& Impact, const FVector& Direction);
	virtual void BeginPlay() override;
	virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* DamageInstigator, AActor* Causer) override;

private:
	bool bBroken = false;
};
