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
	/**
	 * @brief 设置 HUD 护盾值
	 * @param Shield 当前护盾值
	 * @param MaxShield 最大护盾值
	 */
	void SetHUDShield(float Shield, float MaxShield);
	
protected:
	/// 玩家控制角色时调用
	virtual void OnPossess(APawn* InPawn) override;
	
	/// 每帧更新
	virtual void Tick(float DeltaTime) override;
	
	/// 轮询初始化（等待必要组件加载）
	void PollInit();
	
private:
	/** HUD引用 */
	UPROPERTY()
	class ABrokenEdgeHUD* BrokenEdgeHUD;
	
	/// 角色覆盖层 Widget
	UPROPERTY()
	class UCharacterOverlay* CharacterOverlay;
	
	/** 是否初始化生命值 */
	bool bInitializeHealth = false;
	
	/** HUD生命值 */
	float HUDHealth;
	
	/** HUD最大生命值 */
	float HUDMaxHealth;
	
	/// HUD 护盾值
	float HUDShield;
	
	/** 是否初始化护盾值 */
	bool bInitializeShield = false;

	/// HUD 最大护盾值
	float HUDMaxShield;
	
};
