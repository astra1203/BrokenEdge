#pragma once

#include "CoreMinimal.h"
#include "BrokenEdge/GameState/BrokenEdgeGameState.h"
#include "BrokenEdge/PlayerState/BrokenEdgePlayerState.h"
#include "ExtractionProgression.h"
#include "ExtractionState.generated.h"

UENUM(BlueprintType)
enum class EExtractionPhase : uint8 { Waiting, Active, Ended };

UENUM(BlueprintType)
enum class EExtractionLife : uint8 { Alive, Dead, Left, Extracted, TimedOut, Downed };

UCLASS()
class BROKENEDGE_API AExtractionGameState : public ABrokenEdgeGameState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(Replicated, BlueprintReadOnly)
	EExtractionPhase Phase = EExtractionPhase::Waiting;
	UPROPERTY(Replicated, BlueprintReadOnly)
	double Deadline = 0;
	UPROPERTY(Replicated, BlueprintReadOnly)
	int32 AlivePlayers = 0;
	UPROPERTY(Replicated, BlueprintReadOnly)
	FString EndReason;
	UPROPERTY(Replicated)
	FString StorageError;
};

UCLASS()
class BROKENEDGE_API AExtractionPlayerState : public ABrokenEdgePlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(Replicated, BlueprintReadOnly)
	EExtractionLife Life = EExtractionLife::Alive;
	UPROPERTY(Replicated)
	double ExtractAt = 0;
	UPROPERTY(Replicated) FName ExtractExitId;
	UPROPERTY(Replicated) double BleedAt = 0;
	UPROPERTY(Replicated) float DownHealth = 0;
	UPROPERTY(Replicated) FExtractionRaidProgress RaidProgress;
	AExtractionPlayerState();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<class UExtractionInventoryComponent> Inventory;
};
