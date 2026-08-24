// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "BrokenEdgeGameMode.generated.h"

/**
 *
 */
UCLASS()
class BROKENEDGE_API ABrokenEdgeGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	ABrokenEdgeGameMode();
	
	virtual void BeginPlay() override;
	
	/** 热身时间（秒） */
	UPROPERTY(EditDefaultsOnly)
	float WarmupTime = 10.f;

	/** 比赛时间（秒） */
	UPROPERTY(EditDefaultsOnly)
	float MatchTime = 120.f;

	/** 冷却时间（秒） */
	UPROPERTY(EditDefaultsOnly)
	float CooldownTime = 10.f;

	/** 关卡开始时间 */
	float LevelStartingTime = 0.f;

	/** 是否为组队比赛 */
	bool bTeamsMatch = false;

};
