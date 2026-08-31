// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/// 骨骼网格体碰撞通道（用于射线检测）
#define ECC_SkeletalMesh ECollisionChannel::ECC_GameTraceChannel1

/// 命中盒碰撞通道（用于服务端回退时的命中检测）
#define ECC_HitBox ECollisionChannel::ECC_GameTraceChannel2

FString PrintNetMode();