/**
 * @file CombatState.h
 * @brief 战斗状态枚举定义
 * @details 定义角色在战斗中的状态，用于控制玩家输入响应和动画状态
 */

#pragma once

/**
 * @brief 战斗状态枚举
 * @details 用于标识角色当前的战斗状态，控制武器切换、射击、换弹等操作的可用性
 */
UENUM()
enum class ECombatState : uint8
{
	ECS_Unoccuiped UMETA(DisplayName = "Unoccupied"),    ///< 空闲状态（可进行任何操作）
	ECS_Reloading UMETA(DisplayName = "Reloading"),      ///< 正在换弹（禁止射击）
	ECS_ThrowingGrenade UMETA(DisplayName = "Throwing Grenade"), ///< 正在扔手雷（禁止其他操作）
	ECS_SwappingWeapon UMETA(DisplayName = "Swapping Weapon"),   ///< 正在切换武器（禁止射击）

	ECS_MAX UMETA(DisplayName = "DefaultMax")            ///< 默认最大值
};