// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BrokenEdgeHUD.generated.h"

/**
 * @struct FHUDPackage
 * @brief HUD数据包
 * 
 * 包含准星纹理和颜色信息
 */
USTRUCT(BlueprintType)
struct FHUDPackage
{
	GENERATED_BODY()
public:
	/** 准星中心纹理 */
	class UTexture2D* CrosshairsCenter;
	/** 准星左侧纹理 */
	UTexture2D* CrosshairsLeft;
	/** 准星右侧纹理 */
	UTexture2D* CrosshairsRight;
	/** 准星顶部纹理 */
	UTexture2D* CrosshairsTop;
	/** 准星底部纹理 */
	UTexture2D* CrosshairsBottom;
	/** 准星扩散程度 */
	float CrosshairSpread;
	/** 准星颜色 */
	FLinearColor CrosshairsColor;
};

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
	/**
 * @brief 绘制HUD
 */
	virtual void DrawHUD() override;
	
	/** 角色覆盖层widget类 */
	UPROPERTY(EditAnywhere, Category = "Player States")
	TSubclassOf<UUserWidget> CharacterOverlayClass;

	/** 添加角色覆盖层 */
	void AddCharacterOverlay();

	/** 角色覆盖层实例 */
	UPROPERTY()
	class UCharacterOverlay* CharacterOverlay;
	
private:
	/** HUD数据包 */
	FHUDPackage HUDPackage;
	
	/**
 * @brief 绘制准星
 * @param Texture 准星纹理
 * @param ViewportCenter 视口中心
 * @param Spread 扩散程度
 * @param CorsshairColor 准星颜色
 */
	void DrawCrosshair(UTexture2D* Texture, FVector2D ViewportCenter, FVector2D Spread, FLinearColor CorsshairColor);

	
	/** 准星最大扩散程度 */
	UPROPERTY(EditAnywhere)
	float CrosshairSpreadMax = 16.f;

public:
	/**
	 * @brief 设置HUD数据包
	 * @param Package HUD数据包
	 */
	FORCEINLINE void SetHUDPackage(const FHUDPackage& Package) { HUDPackage = Package; }
};
