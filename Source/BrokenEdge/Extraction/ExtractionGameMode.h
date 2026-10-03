#pragma once

#include "CoreMinimal.h"
#include "BrokenEdge/GameMode/BrokenEdgeGameMode.h"
#include "ExtractionPersistence.h"
#include "ExtractionGameMode.generated.h"

UCLASS()
class BROKENEDGE_API AExtractionGameMode : public ABrokenEdgeGameMode
{
	GENERATED_BODY()

public:
	AExtractionGameMode();
	virtual void InitGame(const FString& Map, const FString& Options, FString& Error) override;
	virtual void BeginPlay() override;
	virtual void Tick(float Delta) override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& Id,
	                      FString& Error) override;
	virtual void Logout(AController* Exiting) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* Player) override;
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void PlayerDefeated(class ABrokenEdgeCharacter* Character, class ABrokenEdgePlayerController* Victim,
	                            ABrokenEdgePlayerController* Attacker) override;
	virtual void RequestRespawn(ACharacter* Character, AController* Controller) override;
	virtual float CalculateDamage(AController* Attacker, AController* Victim, float Damage) override;
	void LeavePlayer(AController* Player);
	void FinishRound(const FString& Reason);
	bool IsCombatActive(AController* Player) const;
	bool SettlePlayer(AController* Player, EExtractionOutcome Outcome);
	class UExtractionPersistence* Store() const;

private:
	UPROPERTY()
	TArray<TObjectPtr<class UExtractionItemDefinition>> ItemDefinitions;
	double LoadedAt = 0;
	double RoundSeconds = 600;
	double ReturnAt = 0;
	int32 ExpectedPlayers = 2;
	bool bTravelRequested = false;
	bool bEndRequested = false;
	bool bEndingPlay = false;
	UPROPERTY()
	TObjectPtr<class AExtractionExit> Exit;
	void UpdateExtraction(double Now);
	TSet<TWeakObjectPtr<AActor>> ClaimedStarts;
	void FreezePlayer(AController* Player, bool bFreeze);
	void RefreshAliveCount();
	UFUNCTION()
	void SessionEnded(bool bSuccess);
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
};
