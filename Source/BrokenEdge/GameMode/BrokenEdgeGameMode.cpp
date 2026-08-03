// Fill out your copyright notice in the Description page of Project Settings.


#include "BrokenEdgeGameMode.h"
#include "UObject/ConstructorHelpers.h"

ABrokenEdgeGameMode::ABrokenEdgeGameMode()
{
	static ConstructorHelpers::FClassFinder<APawn> PlayerCharacterClass(
		TEXT("/Game/LearningKit_Games/Blueprints/PlayerCharacter/BP_LearningKit_PlayerCharacter"));

	if (PlayerCharacterClass.Succeeded())
	{
		DefaultPawnClass = PlayerCharacterClass.Class;
	}
}
