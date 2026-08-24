// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BrokenEdge/BrokenEdgeTypes/Team.h"
#include "GameFramework/PlayerState.h"
#include "BrokenEdgePlayerState.generated.h"

/**
 * 
 */
UCLASS()
class BROKENEDGE_API ABrokenEdgePlayerState : public APlayerState
{
	GENERATED_BODY()
public:
	/**
 * 获取需要复制的属性列表
 * @param OutLifetimeProps 输出的属性列表
 */
	virtual void GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const override;

	/**
	 * 添加分数
	 * @param ScoreAmount 要添加的分数
	 */
	void AddToScore(float ScoreAmount);

	/**
	 * 添加击杀数
	 * @param DefeatsAmount 要添加的击杀数
	 */
	void AddToDefeats(int32 DefeatsAmount);
	
private:
	/** 关联的玩家角色 */
	UPROPERTY()
	class ABrokenEdgeCharacter* Character;

	/** 关联的玩家控制器 */
	UPROPERTY()
	class ABrokenEdgePlayerController* Controller;
	
	/** 击杀数，变化时触发OnRep_Defeats */
	UPROPERTY(ReplicatedUsing = OnRep_Defeats)
	int32 Defeats;

	/**
 * 击杀数变化时的复制通知函数
 */
	UFUNCTION()
	virtual void OnRep_Defeats();
	
	/** 队伍归属，变化时触发OnRep_Team */
	UPROPERTY(ReplicatedUsing = OnRep_Team)
	ETeam Team = ETeam::ET_NoTeam;

	/** 队伍变化时的复制通知函数 */
	UFUNCTION()
	void OnRep_Team();
	
	/**
 * 分数变化时的复制通知函数
 */
	virtual void OnRep_Score() override;
	
};
