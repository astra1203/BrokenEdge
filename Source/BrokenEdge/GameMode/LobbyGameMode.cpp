// Fill out your copyright notice in the Description page of Project Settings.


#include "LobbyGameMode.h"

#include "MultiplayerSessionsSubsystem.h"
#include "GameFramework/GameStateBase.h"


void ALobbyGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	int32 NumberOfPlayer = GameState.Get()->PlayerArray.Num();

	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance == nullptr)return;

	UMultiplayerSessionsSubsystem* Subsystem = GameInstance->GetSubsystem<UMultiplayerSessionsSubsystem>();

	check(Subsystem);//我断定Subsystem不是空指针

	// if (NumberOfPlayer==Subsystem->DesiredNumPublicConnections)
	if (NumberOfPlayer==4)
	{
		UWorld* World = GetWorld();
		if (World == nullptr)return;

		bUseSeamlessTravel = true;

		FString MatchType = Subsystem->DesiredMatchType;

		if (MatchType == TEXT("MatchType"))
		{
			World->ServerTravel(FString("/Game/Maps/FreeForAll?listen"));
		}
		else if (MatchType == TEXT("Teams"))
		{
			World->ServerTravel(FString("/Game/Maps/Teams?listen"));
		}


	}


}
