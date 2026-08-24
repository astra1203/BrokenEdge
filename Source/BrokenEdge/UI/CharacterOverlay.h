// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CharacterOverlay.generated.h"

/**
 *
 */
UCLASS()
class BROKENEDGE_API UCharacterOverlay : public UUserWidget
{
	GENERATED_BODY()


public:
	/** 生命值进度条 */
	UPROPERTY(meta = (BindWidget))
	class UProgressBar* HealthBar;

	/** 生命值文本 */
	UPROPERTY(meta = (BindWidget))
	class UTextBlock* HealthText;

	/// 护盾进度条
	UPROPERTY(meta = (BindWidget))
	UProgressBar* ShieldBar;

	/// 护盾文本
	UPROPERTY(meta = (BindWidget))
	UTextBlock* ShieldText;

	/** 当前武器弹药数 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* WeaponAmmoAmount;

	/** 携带弹药数 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* CarriedAmmoAmount;
	
	/** 分数文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* ScoreAmount;
	
	/** 击杀数文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* DefeatsAmount;
	
	/** 比赛倒计时文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* MatchCountdownText;
	
	/** 红队分数文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* RedTeamScore;

	/** 蓝队分数文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* BlueTeamScore;
	
};
