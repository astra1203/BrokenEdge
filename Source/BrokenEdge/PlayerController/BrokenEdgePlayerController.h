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
	
	void SetHUDWeaponAmmo(int32 Ammo);
	
	void SetHUDCarriedAmmo(int32 Ammo);

	/**
 * @brief 设置HUD分数显示
 * @param Score 分数值
 */
	void SetHUDScore(float Score);
	
	/**
 * @brief 设置HUD淘汰数显示
 * @param Defeats 淘汰数
 */
	void SetHUDDefeats(int32 Defeats);
	
	
	void SetHUDBlueTeamScore(int32 BlueScore);
	void SetHUDRedTeamScore(int32 RedScore);

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
	float HUDHealth = 0.f;
	
	/** HUD最大生命值 */
	float HUDMaxHealth = 0.f;
	
	/// HUD 护盾值
	float HUDShield = 0.f;
	
	/** 是否初始化护盾值 */
	bool bInitializeShield = false;

	/// HUD 最大护盾值
	float HUDMaxShield = 0.f;
	
	/** 是否初始化武器弹药 */
	bool bInitializeWeaponAmmo = false;

	/** HUD武器弹药 */
	float HUDWeaponAmmo = 0.f;
	
	/** HUD携带弹药 */
	float HUDCarriedAmmo = 0.f;

	/** 是否初始化携带弹药 */
	bool bInitializeCarriedAmmo = false;
	
	/** 是否初始化分数 */
	bool bInitializeScore = false;
	
	/** HUD分数 */
	float HUDScore;
	
	/** 是否初始化淘汰数 */
	bool bInitializeDefeats = false;

	/** HUD淘汰数 */
	int32 HUDDefeats;
};
