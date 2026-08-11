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
public:
	/**
    * @brief 设置HUD生命值显示
    * @param Health 当前生命值
    * @param MaxHealth 最大生命值
    */
	void SetHUDHealth(float Health, float MaxHealth);
	
private:
	/** HUD引用 */
	UPROPERTY()
	class ABrokenEdgeHUD* BrokenEdgeHUD;
	
	/** 是否初始化生命值 */
	bool bInitializeHealth = false;
	
	/** HUD生命值 */
	float HUDHealth;
	
	/** HUD最大生命值 */
	float HUDMaxHealth;
	
};
