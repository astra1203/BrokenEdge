// Fill out your copyright notice in the Description page of Project Settings.


#include "BrokenEdgePlayerState.h"

#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "Net/UnrealNetwork.h"

void ABrokenEdgePlayerState::CopyProperties(APlayerState* Target)
{
	Super::CopyProperties(Target);
	if (auto* Other = Cast<ABrokenEdgePlayerState>(Target))
	{
		Other->ExtractionProfile = ExtractionProfile;
		Other->ExtractionRaid = ExtractionRaid;
		Other->ExtractionSquad = ExtractionSquad;
	}
}


void ABrokenEdgePlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABrokenEdgePlayerState, Team);
	DOREPLIFETIME(ABrokenEdgePlayerState, ExtractionSquad);
	DOREPLIFETIME(ABrokenEdgePlayerState, Defeats);
	DOREPLIFETIME(ABrokenEdgePlayerState, bLobbyReady);
}

void ABrokenEdgePlayerState::SetLobbyReady(bool bReady)
{
	if (!HasAuthority()) return;
	bLobbyReady = bReady;
	ForceNetUpdate();
}

void ABrokenEdgePlayerState::AddToScore(float ScoreAmount)
{
	SetScore(GetScore() + ScoreAmount);
	Character = Character == nullptr ? Cast<ABrokenEdgeCharacter>(GetPawn()) : Character;
	if (Character)
	{
		Controller = Controller == nullptr ? Cast<ABrokenEdgePlayerController>(Character->Controller) : Controller;
		if (Controller)
		{
			Controller->SetHUDScore(GetScore());
		}
	}
}

void ABrokenEdgePlayerState::AddToDefeats(int32 DefeatsAmount)
{
	Defeats += DefeatsAmount;
	Character = Character == nullptr ? Cast<ABrokenEdgeCharacter>(GetPawn()) : Character;
	if (Character)
	{
		Controller = Controller == nullptr ? Cast<ABrokenEdgePlayerController>(Character->Controller) : Controller;
		if (Controller)
		{
			Controller->SetHUDDefeats(Defeats);
		}
	}
}

void ABrokenEdgePlayerState::OnRep_Defeats()
{
	Character = Character == nullptr ? Cast<ABrokenEdgeCharacter>(GetPawn()) : Character;
	if (Character)
	{
		Controller = Controller == nullptr ? Cast<ABrokenEdgePlayerController>(Character->Controller) : Controller;
		if (Controller)
		{
			Controller->SetHUDDefeats(Defeats);
		}
	}
}

void ABrokenEdgePlayerState::OnRep_Team()
{
	//@TODO
}

void ABrokenEdgePlayerState::OnRep_Score()
{
	Super::OnRep_Score();
	
	Character = Character == nullptr ? Cast<ABrokenEdgeCharacter>(GetPawn()) : Character;
	if (Character)
	{
		Controller = Controller == nullptr ? Cast<ABrokenEdgePlayerController>(Character->Controller) : Controller;
		if (Controller)
		{
			Controller->SetHUDScore(GetScore());
		}
	}
	
	
}
