// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UPrimitiveComponent;

/// 骨骼网格体碰撞通道（用于射线检测）
#define ECC_SkeletalMesh ECollisionChannel::ECC_GameTraceChannel1

/// 命中盒碰撞通道（用于服务端回退时的命中检测）
#define ECC_HitBox ECollisionChannel::ECC_GameTraceChannel2

FString PrintNetMode();

/**
 * 将碰撞组件当前生效的碰撞预设设置打印到 Output Log。
 * 包括预设名、碰撞启用方式、对象类型、事件开关和各碰撞通道响应。
 */
BROKENEDGE_API void PrintCollisionPresetSettings(const UPrimitiveComponent* Component);
