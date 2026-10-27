#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ExtractionTacticalMap.generated.h"
// 战术地图是地图定义与对局复制状态的只读投影，显示楼层、撤离点和同队玩家。
// 地图绘制不驱动撤离判定；开放时间和玩家位置来自实际对局状态。

UCLASS()
class BROKENEDGE_API UExtractionTacticalMap : public UUserWidget
{
	GENERATED_BODY()
public:
	// 加载当前实现固定指定的地图数据资产，再创建 Slate 绘图面板；资产引用由 UPROPERTY 持有。
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UPROPERTY(Transient)
	TObjectPtr<class UExtractionMapDefinition> Definition;
};
