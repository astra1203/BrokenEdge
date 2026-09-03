// Fill out your copyright notice in the Description page of Project Settings.


#include "BrokenEdgeGameState.h"

#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "Net/UnrealNetwork.h"


void ABrokenEdgeGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME(ABrokenEdgeGameState, RedTeamScore);
	DOREPLIFETIME(ABrokenEdgeGameState, BlueTeamScore);
}

void ABrokenEdgeGameState::AddBlueTeamScores()
{
	++BlueTeamScore;
	ABrokenEdgePlayerController* BrokenEdgePlayerController = Cast<ABrokenEdgePlayerController>(GetWorld()->GetFirstPlayerController());
	if (BrokenEdgePlayerController)
	{
		BrokenEdgePlayerController->SetHUDBlueTeamScore(BlueTeamScore);
	}
}

void ABrokenEdgeGameState::AddRedTeamScores()
{
	++RedTeamScore;
	ABrokenEdgePlayerController* BrokenEdgePlayerController = Cast<ABrokenEdgePlayerController>(GetWorld()->GetFirstPlayerController());
	if (BrokenEdgePlayerController)
	{
		BrokenEdgePlayerController->SetHUDRedTeamScore(RedTeamScore);
	}
}

void ABrokenEdgeGameState::OnRep_RedTeamScore()
{
	ABrokenEdgePlayerController* BrokenEdgePlayerController = Cast<ABrokenEdgePlayerController>(GetWorld()->GetFirstPlayerController());
	if (BrokenEdgePlayerController)
	{
		BrokenEdgePlayerController->SetHUDRedTeamScore(RedTeamScore);
	}

}

void ABrokenEdgeGameState::OnRep_BlueTeamScore()
{	
	ABrokenEdgePlayerController* BrokenEdgePlayerController = Cast<ABrokenEdgePlayerController>(GetWorld()->GetFirstPlayerController());
	if (BrokenEdgePlayerController)
	{
		BrokenEdgePlayerController->SetHUDBlueTeamScore(BlueTeamScore);
	}
}
