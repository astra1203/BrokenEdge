#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LobbyShowcase.generated.h"

/** Local presentation only. Never spawns a pawn, owns items, or submits account requests. */
UCLASS(NotBlueprintable)
class BROKENEDGE_API ALobbyShowcase : public AActor
{
	GENERATED_BODY()

public:
	ALobbyShowcase();
	virtual void BeginPlay() override;
	void SetSquadSize(int32 Count);
	void SetHeldWeapon(FName Definition);
	FName GetHeldDefinition() const { return HeldDefinition; }
	UPROPERTY(Transient)
	TObjectPtr<class UTextureRenderTarget2D> Portrait;

private:
	UPROPERTY()
	TArray<TObjectPtr<class USkeletalMeshComponent>> Members;
	UPROPERTY()
	TObjectPtr<class USceneCaptureComponent2D> Capture;
	UPROPERTY()
	TObjectPtr<class UAnimSequence> Idle;
	UPROPERTY()
	TObjectPtr<class UAnimSequence> RifleIdle;
	UPROPERTY()
	TObjectPtr<class USkeletalMeshComponent> Equipment;
	FName HeldDefinition;
};
