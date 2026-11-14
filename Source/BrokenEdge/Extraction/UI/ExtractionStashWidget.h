#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ExtractionStashWidget.generated.h"
// 大厅仓库界面读取 Account 的档案快照，展示仓库、出战装备和溢出物品。
// 界面负责拖拽与选择，Account 负责带版本号的请求；锁定档案期间禁止编辑，防止与出战结算竞争。

UCLASS()
class BROKENEDGE_API UExtractionStashWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 把键盘焦点交给仓库面板，以接收旋转、拆分和退出操作。
	void FocusPanel();
	// 创建仓库 Slate 面板，保留 UMG 生命周期管理入口。
	virtual TSharedRef<SWidget> RebuildWidget() override;
	// 释放面板引用，支持关闭或重建界面。
	virtual void ReleaseSlateResources(bool Children) override;
	// 从所属玩家控制器查找账号组件，所有仓库操作使用同一份同步档案。
	class UExtractionAccountComponent* Account() const;

private:
	TSharedPtr<class SExtractionStashPanel> Panel;
};
