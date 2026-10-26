#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ExtractionNavigation.generated.h"
// 编辑器准备工具，负责构建导航并逐段检查巡逻路线；运行时或打包版本调用返回 false。
// BlueprintFunctionLibrary 提供静态入口，WorldContext 参数让蓝图/Python 工具提供目标编辑器世界。

UCLASS()
class BROKENEDGE_API UExtractionNavigation : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Editor preparation only; does not rebuild runtime navigation. */
	// 等待异步资产编译后仅解除 AsyncLoadLock，保留其他导航构建锁；构建后验证每条路线相邻点可完整寻路。
	// 成功只证明这里检查的路线段连通，不代表整图所有区域通行已验收。
	UFUNCTION(BlueprintCallable, meta=(WorldContext="Context"))
	static bool BuildNavigation(UObject* Context);
};
