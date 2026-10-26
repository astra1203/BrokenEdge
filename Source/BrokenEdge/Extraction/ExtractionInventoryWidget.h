#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ExtractionInventoryWidget.generated.h"
// 局内背包的 UMG 外壳与 Slate 交互面板：读取背包及搜索后可见的战利品快照，提交物品操作意图。
// 拖拽、旋转、菜单和拆分数量都是本地界面状态；真正的容器变更由服务器校验并复制回来。

class SExtractionInventoryPanel;

/** Native, resolution-scaled inventory; all mutations go through production RPCs. */
UCLASS()
class BROKENEDGE_API UExtractionInventoryWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	// 创建 Slate 面板并传入所属 UWidget，面板通过控制器读取当前局内状态。
	virtual TSharedRef<SWidget> RebuildWidget() override;
	// 释放 Slate 共享引用，避免 UWidget 重建后继续持有旧面板。
	virtual void ReleaseSlateResources(bool ReleaseChildren) override;
	// 根据背包开关调整可见性并刷新面板，关闭时保留不拦截鼠标的提示显示。
	void Refresh();
	// 将外部取消操作转交面板，统一清理拖拽和弹出菜单状态。
	bool CancelDrag();
private:
	TSharedPtr<SExtractionInventoryPanel> Panel;
};
