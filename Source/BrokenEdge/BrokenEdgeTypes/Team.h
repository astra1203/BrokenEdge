/**
* @file Team.h
 * @brief 队伍枚举定义
 * 
 * 定义玩家所属的队伍类型，用于团队对战模式中的队伍区分
 */
#pragma once

/**
 * @brief 队伍枚举
 * 
 * 描述玩家或游戏对象所属的队伍，用于团队对战模式
 */
UENUM(BlueprintType)
enum class ETeam : uint8
{
	ET_RedTeam UMETA(DisplayName = "RedTeam"),   ///< 红队
	ET_BlueTeam UMETA(DisplayName = "BlueTeam"), ///< 蓝队
	ET_NoTeam UMETA(DisplayName = "NoTeam"),     ///< 无队伍

	ET_MAX UMETA(DisplayName = "DefaultMAX")     ///< 枚举最大值
};