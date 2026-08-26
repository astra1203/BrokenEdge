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
	
	/**
 * 计算伤害
 * @param Attacker 攻击者控制器
 * @param Victim 受害者控制器
 * @param BaseDamage 基础伤害
 * @return 最终伤害值
 */
	virtual float CalculateDamage(AController* Attacker, AController* Victim, float BaseDamage);

	/**
 * 玩家被淘汰
 * @param DefeatedCharacter 被淘汰的角色
 * @param DefeatedController 受害者控制器
 * @param AttackerController 攻击者控制器
 */
	virtual void PlayerDefeated(class ABrokenEdgeCharacter* DefeatedCharacter,
		class ABrokenEdgePlayerController* DefeatedController,
		ABrokenEdgePlayerController* AttackerController);


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
