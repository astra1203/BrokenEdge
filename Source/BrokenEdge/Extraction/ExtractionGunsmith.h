#pragma once
#include "CoreMinimal.h"
#include "ExtractionItems.h"
// 改枪面板通过 Read/Submit 回调复用局内背包和大厅仓库的数据入口，不持有可直接修改的真实容器。
// 属性对比在容器副本上执行 Configure；点击应用才发送请求，并等待版本变化或等待提示超时。

class SWidget;
namespace ExtractionGunsmith
{
	// Snapshot reads and production transaction callbacks are supplied by inventory/stash hosts.
	// 关闭已有改枪面板，在游戏视口创建居中的新面板并设置键盘焦点。
	void Open(TSharedRef<SWidget> Parent, FGuid Weapon, TFunction<FExtractionContainer()> Read,
	          TFunction<void(const FExtractionItemRequest&)> Submit);
}
