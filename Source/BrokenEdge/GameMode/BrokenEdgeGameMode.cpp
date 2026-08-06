// Fill out your copyright notice in the Description page of Project Settings.


#include "BrokenEdgeGameMode.h"

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
	UCombatObserver* CombatObserver = NewObject<UCombatObserver>();

	const FTransform SpawnTransform(
		FRotator::ZeroRotator,
		FVector(0.0f, 0.0f, 100.0f));

	AMyCharacter* PlayerCharacter = GetWorld()->SpawnActor<AMyCharacter>(
		AMyCharacter::StaticClass(),
		SpawnTransform);

	CombatObserver->StartObserving(PlayerCharacter);

	PlayerCharacter->TakeDamage(10.0f);

}
