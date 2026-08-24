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
	
	/**
 * 玩家登录后调用（重写）
 * @param NewPlayer 新登录的玩家控制器
 */
	virtual void PostLogin(APlayerController* NewPlayer) override;

	
	
};
