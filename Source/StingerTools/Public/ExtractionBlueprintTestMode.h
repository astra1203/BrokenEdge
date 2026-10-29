#pragma once

#include "BrokenEdge/Extraction/ExtractionGameMode.h"
#include "ExtractionBlueprintTestMode.generated.h"

/** Editor-only lifecycle fixture. Uses production controllers/state/loot without raid tickets. */
UCLASS(NotBlueprintable, Transient)
class AExtractionBlueprintTestMode : public AExtractionGameMode
{
	GENERATED_BODY()
public:
	AExtractionBlueprintTestMode();
	virtual void InitGame(const FString& Map, const FString& Options, FString& Error) override { AGameMode::InitGame(Map, Options, Error); }
	virtual void BeginPlay() override { AGameMode::BeginPlay(); }
	virtual void Tick(float Delta) override { AGameMode::Tick(Delta); }
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& Id, FString& Error) override { AGameMode::PreLogin(Options, Address, Id, Error); }
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* Player) override { AGameMode::HandleStartingNewPlayer_Implementation(Player); }
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override { return AGameMode::PlayerCanRestart_Implementation(Player); }
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override { return AGameMode::ChoosePlayerStart_Implementation(Player); }
	virtual AActor* FindPlayerStart_Implementation(AController* Player, const FString& Name) override { return AGameMode::FindPlayerStart_Implementation(Player, Name); }
};
