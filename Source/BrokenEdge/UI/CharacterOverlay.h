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


};
