// Fill out your copyright notice in the Description page of Project Settings.


#include "BrokenEdgeGameMode.h"

#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/GameState/BrokenEdgeGameState.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "BrokenEdge/PlayerState/BrokenEdgePlayerState.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
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

	}

	if (DefeatedCharacter)
	{
		DefeatedCharacter->Defeated(false);
	}

	for (auto It=GetWorld()->GetPlayerControllerIterator();It;It++)
	{
		ABrokenEdgePlayerController* PlayerController = Cast<ABrokenEdgePlayerController>(*It);

		if (PlayerController&&AttackerPlayerState&&DefeatedPlayerState)
		{
			PlayerController->ClientDefeatedAnnouncement(AttackerPlayerState,DefeatedPlayerState);
		}

	}

}

void ABrokenEdgeGameMode::RequestRespawn(ACharacter* DefeatedCharacter, AController* DefeatedController)
{
	if (!IsValid(DefeatedController)) return;
	if (IsValid(DefeatedCharacter))
	{
		DefeatedCharacter->Reset();
		DefeatedCharacter->Destroy();
	}
	
	if (DefeatedController)
	{
		TArray<AActor*> PlayerStarts;
		UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), PlayerStarts);
		if (PlayerStarts.IsEmpty())
		{
			UE_LOG(LogTemp, Error, TEXT("RESPAWN_NO_PLAYERSTART Map=%s Controller=%s"),
			       *GetWorld()->GetMapName(), *GetNameSafe(DefeatedController));
			// FindPlayerStart falls back to WorldSettings (the origin) in UE 5.8.
			// Preserve custom/cached starts, but never accept that synthetic fallback.
			AActor* Fallback = FindPlayerStart(DefeatedController);
			if (IsValid(Fallback) && !Fallback->IsA<AWorldSettings>())
				RestartPlayerAtPlayerStart(DefeatedController, Fallback);
			return;
		}
		
		int32 Selection = FMath::RandRange(0, PlayerStarts.Num() - 1);
		
		RestartPlayerAtPlayerStart(DefeatedController,PlayerStarts[Selection]);
		
	}
	

}
