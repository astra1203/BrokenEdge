#pragma once
#include "ExtractionGameMode.h"
#include "WeaponRangeGameMode.generated.h"

/** Disposable local training mode. No raid tickets, rewards or settlement. */
UCLASS()
class BROKENEDGE_API AWeaponRangeGameMode : public AExtractionGameMode
{
	GENERATED_BODY()

public:
	virtual void InitGame(const FString& Map, const FString& Options, FString& Error) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* Player) override;
	virtual void Tick(float Delta) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
};
