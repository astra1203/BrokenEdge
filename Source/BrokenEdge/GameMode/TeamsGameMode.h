// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BrokenEdgeGameMode.h"
#include "TeamsGameMode.generated.h"

/**
 * 
 */
UCLASS()
class BROKENEDGE_API ATeamsGameMode : public ABrokenEdgeGameMode
{
	GENERATED_BODY()
	
public:
	ATeamsGameMode();
	/**
 * 玩家登录后调用（重写）
 * @param NewPlayer 新登录的玩家控制器
 */
	virtual void PostLogin(APlayerController* NewPlayer) override;

	/**
 * 玩家退出时调用（重写）
 * @param Exiting 退出的控制器
 */
	virtual void Logout(AController* Exiting) override;

	virtual void PlayerDefeated(ABrokenEdgeCharacter* DefeatedCharacter,
		ABrokenEdgePlayerController* DefeatedController, ABrokenEdgePlayerController* AttackerController) override;

	virtual float CalculateDamage(AController* Attacker, AController* Victim, float BaseDamage) override;
};
