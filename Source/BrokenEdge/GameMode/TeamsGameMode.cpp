// Fill out your copyright notice in the Description page of Project Settings.


#include "TeamsGameMode.h"

#include "BrokenEdge/BrokenEdgeTypes/Team.h"
#include "BrokenEdge/GameState/BrokenEdgeGameState.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "BrokenEdge/PlayerState/BrokenEdgePlayerState.h"

ATeamsGameMode::ATeamsGameMode()
{
	bTeamsMatch = true;
}

/**
 * @brief 玩家登录后的处理，分配队伍
 * @param NewPlayer 新登录的玩家控制器
 */
void ATeamsGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT(""));
	ABrokenEdgeGameState* BrokenEdgeGameState = GetGameState<ABrokenEdgeGameState>();

	if (BrokenEdgeGameState == nullptr)return;
	//@TODO

	ABrokenEdgePlayerState* NewPlayerState = NewPlayer->GetPlayerState<ABrokenEdgePlayerState>();

	if (NewPlayerState && NewPlayerState->GetTeam() == ETeam::ET_NoTeam)
	{
		if (BrokenEdgeGameState->RedTeam.Num() >= BrokenEdgeGameState->BlueTeam.Num())
		{
			NewPlayerState->SetTeam(ETeam::ET_BlueTeam);
			BrokenEdgeGameState->BlueTeam.AddUnique(NewPlayerState);
			UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
			       TEXT("玩家被分配到蓝队"));
		}
		else
		{
			NewPlayerState->SetTeam(ETeam::ET_RedTeam);
			BrokenEdgeGameState->RedTeam.AddUnique(NewPlayerState);
			UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
			       TEXT("玩家被分配到红队"));
		}
	}
}

void ATeamsGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);

	ABrokenEdgeGameState* BrokenEdgeGameState = GetGameState<ABrokenEdgeGameState>();
	ABrokenEdgePlayerState* ExitingPlayerState = Exiting->GetPlayerState<ABrokenEdgePlayerState>();
	if (BrokenEdgeGameState && ExitingPlayerState)
	{
		if (BrokenEdgeGameState->RedTeam.Contains(ExitingPlayerState))
		{
			BrokenEdgeGameState->RedTeam.Remove(ExitingPlayerState);
		}

		if (BrokenEdgeGameState->BlueTeam.Contains(ExitingPlayerState))
		{
			BrokenEdgeGameState->BlueTeam.Remove(ExitingPlayerState);
		}
	}
}

void ATeamsGameMode::PlayerDefeated(ABrokenEdgeCharacter* DefeatedCharacter,
                                    ABrokenEdgePlayerController* DefeatedController,
                                    ABrokenEdgePlayerController* AttackerController)
{
	Super::PlayerDefeated(DefeatedCharacter, DefeatedController, AttackerController);

	ABrokenEdgeGameState* BrokenEdgeGameState = GetGameState<ABrokenEdgeGameState>();

	ABrokenEdgePlayerState* AttackerPlayerState = AttackerController
		                                              ? AttackerController->GetPlayerState<ABrokenEdgePlayerState>()
		                                              : nullptr;

	if (BrokenEdgeGameState && AttackerPlayerState)
	{
		if (AttackerPlayerState->GetTeam() == ETeam::ET_RedTeam)
		{
			BrokenEdgeGameState->AddRedTeamScores();
		}
		else if (AttackerPlayerState->GetTeam() == ETeam::ET_BlueTeam)
		{
			BrokenEdgeGameState->AddBlueTeamScores();
		}
	}
}

float ATeamsGameMode::CalculateDamage(AController* Attacker, AController* Victim, float BaseDamage)
{
	ABrokenEdgePlayerState* AttackerPlayerState = Attacker->GetPlayerState<ABrokenEdgePlayerState>();
	ABrokenEdgePlayerState* VictimPlayerState = Victim->GetPlayerState<ABrokenEdgePlayerState>();
	if (AttackerPlayerState == nullptr || VictimPlayerState == nullptr)return BaseDamage;

	if (Attacker == Victim)
	{
		return BaseDamage;
	}
	if (AttackerPlayerState->GetTeam() == VictimPlayerState->GetTeam())
	{
		return 0.f;
	}
	return BaseDamage;
}
