// Fill out your copyright notice in the Description page of Project Settings.


#include "TeamsGameMode.h"

#include "BrokenEdge/GameState/BrokenEdgeGameState.h"

/**
 * @brief 玩家登录后的处理，分配队伍
 * @param NewPlayer 新登录的玩家控制器
 */
void ATeamsGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	
	ABrokenEdgeGameState* BrokenEdgeGameState = GetGameState<ABrokenEdgeGameState>();
	
	if (BrokenEdgeGameState==nullptr)return;
	//@TODO
	
	
	
}
