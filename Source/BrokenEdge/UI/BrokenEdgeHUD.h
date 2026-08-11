// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BrokenEdgeHUD.generated.h"

/**
 *
 */
UCLASS()
class BROKENEDGE_API ABrokenEdgeHUD : public AHUD
{
	GENERATED_BODY()
protected:
	/**
	 * @brief 游戏开始时初始化
	 */
	virtual void BeginPlay() override;

public:
	/** 角色覆盖层widget类 */
	UPROPERTY(EditAnywhere, Category = "Player States")
	TSubclassOf<UUserWidget> CharacterOverlayClass;

	/** 添加角色覆盖层 */
	void AddCharacterOverlay();

	/** 角色覆盖层实例 */
	UPROPERTY()
	class UCharacterOverlay* CharacterOverlay;
};
