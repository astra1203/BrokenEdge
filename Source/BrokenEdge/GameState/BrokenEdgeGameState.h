// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "BrokenEdgeGameState.generated.h"

class ABrokenEdgePlayerState;
/**
 * 
 */
UCLASS()
class BROKENEDGE_API ABrokenEdgeGameState : public AGameState
{
	GENERATED_BODY()
	
public:
	/**
 * 获取需要复制的属性列表
 * @param OutLifetimeProps 输出的属性列表
 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 红队玩家列表 */
	UPROPERTY()
	TArray<ABrokenEdgePlayerState*> RedTeam;

	/** 蓝队玩家列表 */
	UPROPERTY()
	TArray<ABrokenEdgePlayerState*> BlueTeam;
	
	/**
	 * 队伍相关功能
	 */
	void BlueTeamScores();
	void RedTeamScores();
	
	/** 红队分数，变化时触发OnRep_RedTeamScore */
	UPROPERTY(ReplicatedUsing = OnRep_RedTeamScore)
	float RedTeamScore = 0.f;

	/** 蓝队分数，变化时触发OnRep_BlueTeamScore */
	UPROPERTY(ReplicatedUsing = OnRep_BlueTeamScore)
	float BlueTeamScore = 0.f;
	
	/** 红队分数变化时的复制通知函数 */
	UFUNCTION()
	void OnRep_RedTeamScore();

	/** 蓝队分数变化时的复制通知函数 */
	UFUNCTION()
	void OnRep_BlueTeamScore();
};
