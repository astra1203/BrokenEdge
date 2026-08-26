// Fill out your copyright notice in the Description page of Project Settings.


#include "BrokenEdgeGameMode.h"

#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/GameState/BrokenEdgeGameState.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "BrokenEdge/PlayerState/BrokenEdgePlayerState.h"
#include "BrokenEdge/test/CombatObserver.h"
#include "BrokenEdge/test/MyCharacter.h"
#include "UObject/ConstructorHelpers.h"

ABrokenEdgeGameMode::ABrokenEdgeGameMode()
{
	static ConstructorHelpers::FClassFinder<APawn> PlayerCharacterClass(
		TEXT("/Game/LearningKit_Games/Blueprints/PlayerCharacter/BP_LearningKit_PlayerCharacter"));

	if (PlayerCharacterClass.Succeeded())
	{
		DefaultPawnClass = PlayerCharacterClass.Class;
	}
	// GetGameInstance()
	// GetPlayerControllerClassToSpawnForSeamlessTravel()
}

void ABrokenEdgeGameMode::BeginPlay()
{
	Super::BeginPlay();


}

float ABrokenEdgeGameMode::CalculateDamage(AController* Attacker, AController* Victim, float BaseDamage)
{
	return BaseDamage;
}

void ABrokenEdgeGameMode::PlayerDefeated(class ABrokenEdgeCharacter* DefeatedCharacter,
	class ABrokenEdgePlayerController* DefeatedController, ABrokenEdgePlayerController* AttackerController)
{
	ABrokenEdgePlayerState* AttackerPlayerState =
		AttackerController ? Cast<ABrokenEdgePlayerState>(AttackerController->PlayerState) : nullptr;

	ABrokenEdgePlayerState* DefeatedPlayerState =
		DefeatedController ? Cast<ABrokenEdgePlayerState>(DefeatedController->PlayerState) : nullptr;

	ABrokenEdgeGameState* BrokenEdgeGameState = GetGameState<ABrokenEdgeGameState>();

	if (AttackerPlayerState && AttackerPlayerState!=DefeatedPlayerState&&BrokenEdgeGameState)
	{
		AttackerPlayerState->AddToDefeats(1);
		AttackerPlayerState->AddToScore(1);

		//@TODO
		// BrokenEdgeGameState->AddToDefeats(1);

	}

	if (DefeatedCharacter)
	{
		DefeatedCharacter->Defeated(false);
	}

}

void ABrokenEdgeGameMode::RequestRespawn(ACharacter* DefeatedCharacter, AController* DefeatedController)
{
	if (DefeatedCharacter)
	{
		DefeatedCharacter->Reset();
		DefeatedCharacter->Destroy();
	}

}
