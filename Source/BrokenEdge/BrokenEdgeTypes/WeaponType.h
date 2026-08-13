/**
* @file WeaponTypes.h
 * @brief 武器类型及常量定义
 * @details 定义游戏中使用的武器类型枚举、射线追踪长度、自定义深度值等常量
 */

#pragma once


/// 自定义深度值：蓝色（用于武器高亮等）
#define CUSTOM_DEPTH_BLUE 251
/// 自定义深度值：棕褐色（用于武器高亮等）
#define CUSTOM_DEPTH_TAN 252

/**
 * @brief 武器类型枚举
 * @details 定义游戏中所有可用的武器类型，用于武器识别、弹药管理和 HUD 显示
 */
UENUM(BlueprintType)
enum class EWeaponType : uint8
{
	EWT_AssaultRifle UMETA(DisplayName = "Assault Rifle"),      ///< 突击步枪
	EWT_RocketLauncher UMETA(DisplayName = "Rocket Launcher"),  ///< 火箭发射器
	EWT_Pistol UMETA(DisplayName = "Pistol"),                  ///< 手枪
	EWT_SMG UMETA(DisplayName = "SMG"),                        ///< 冲锋枪
	EWT_Shotgun UMETA(DisplayName = "Shotgun"),                ///< 霰弹枪
	EWT_SniperRifle UMETA(DisplayName = "Sniper Rifle"),       ///< 狙击枪
	EWT_GrenadeLauncher UMETA(DisplayName = "Grenade Launcher"),///< 榴弹发射器
	EWT_Flag UMETA(DisplayName = "Flag"),                      ///< 旗帜（夺旗模式用）

	EWT_MAX UMETA(DisplayName = "DefaultMAX")                  ///< 默认最大值（用于数组大小等）
};