// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BrokenEdgePlayerController.generated.h"

/**
 *
 */
UCLASS()
class BROKENEDGE_API ABrokenEdgePlayerController : public APlayerController
{
	GENERATED_BODY()

	/**
    * @brief 设置HUD生命值显示
    * @param Health 当前生命值
    * @param MaxHealth 最大生命值
    */
	void SetHUDHealth(float Health, float MaxHealth);
};
