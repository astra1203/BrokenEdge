#pragma once

#include "CoreMinimal.h"

/** 守卫专用的额外瞄准误差，独立于武器及其附件的散布数值。 */
namespace ExtractionGuardAim
{
	/**
	 * 在既有射线方向上追加均匀圆锥散射，并保持原射线长度。
	 * HalfAngleDegrees 是圆锥半角，不是目标位置的固定厘米偏移，因此距离越远偏差越大。
	 * Seed 由服务器的武器种子和 ShotId 派生；同一发只计算一次，客户端复用最终端点。
	 * 角度为零时精确保留原端点，方便配置关闭这层误差。
	 */
	inline FVector ApplySpread(const FVector& Origin, const FVector& Target, float HalfAngleDegrees, uint32 Seed)
	{
		if (!FMath::IsFinite(HalfAngleDegrees) || HalfAngleDegrees <= 0
			|| Origin.ContainsNaN() || Target.ContainsNaN()) return Target;
		const FVector Delta = Target - Origin;
		const double Distance = Delta.Size();
		if (Distance <= UE_SMALL_NUMBER) return Target;
		FRandomStream Random(static_cast<int32>(Seed));
		const float Radians = FMath::DegreesToRadians(FMath::Clamp(HalfAngleDegrees, 0.f, 45.f));
		return Origin + Random.VRandCone(Delta / Distance, Radians) * Distance;
	}
}
