#pragma once

#include "CoreMinimal.h"

/**
 * BB_ExtractionGuard 的公共键名，与编辑器中的黑板保持一致。
 * 黑板资产仍需在编辑器中创建；这些常量仅避免 Service/Task 各自拼写字符串。
 * 时间全部是当前服务器 World 的 GetTimeSeconds()，不是倒计时剩余秒数。
 * 不要勾选黑板键的 Instance Synced：每名守卫必须持有自己的目标和计时数据。
 */
namespace GuardBlackboard
{
	inline const FName CanRunAI(TEXT("CanRunAi"));
	inline const FName LastKnownLocation(TEXT("LastKnownLocation"));
	inline const FName HasLastKnownLocation(TEXT("HasLastKnownLocation"));
	inline const FName LastSeenTime(TEXT("LastSeenTime"));
	inline const FName SearchActive(TEXT("SearchActive"));
	inline const FName ReactionReadyTime(TEXT("ReactionReadyTime"));
	inline const FName NextFireTime(TEXT("NextFireTime"));
	inline const FName PatrolLocation(TEXT("PatrolLocation"));
	inline const FName ReturningToRoute(TEXT("ReturningToRoute"));
	inline const FName TargetActor(TEXT("TargetActor"));
}
