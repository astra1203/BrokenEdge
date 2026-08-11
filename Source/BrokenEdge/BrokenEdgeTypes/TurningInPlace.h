/**
* @file TurningInPlace.h
 * @brief 原地转向状态枚举
 * @details 定义角色原地转向的状态，用于动画蒙太奇播放和动画蓝图逻辑
 */

#pragma once

/**
 * @brief 原地转向状态枚举
 * @details 用于标识角色是否在原地转向以及转向方向，控制动画过渡
 */
UENUM()
enum class ETurningInPlace : uint8
{
	ETIP_Left UMETA(DisplayName = "Turning Left"),   ///< 正在向左转
	ETIP_Right UMETA(DisplayName = "Turning Right"), ///< 正在向右转
	ETIP_NotTurning UMETA(DisplayName = "Not Turning"), ///< 未在转向

	ETIP_MAX UMETA(DisplayName = "DefaultMAX")       ///< 默认最大值
};

//改动测试，添加改动本地提交，然后推送远程仓库，你们需要pull拉取远程仓库的修改

// 你好

//测试2
