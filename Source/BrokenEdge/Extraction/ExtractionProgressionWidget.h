#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ExtractionProgressionWidget.generated.h"
// 商店与任务页共用一个 Slate 面板，读取账号快照和成长规则展示报价、等级及任务状态。
// 确认窗保存预览时的档案版本，服务器仍会重新校验价格、库存、任务条件并持久化事务。

UCLASS()
class BROKENEDGE_API UExtractionProgressionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 设置面板键盘焦点，支持数量输入和取消确认。
	void FocusPanel();
	bool TasksInitially = false;
	// 创建面板并传入初始标签页选择。
	virtual TSharedRef<SWidget> RebuildWidget() override;
	// 释放 Slate 面板的共享引用。
	virtual void ReleaseSlateResources(bool Children) override;
	// 取得本地玩家的账号组件，用于读取档案和发送经济请求。
	class UExtractionAccountComponent* Account() const;

private:
	TSharedPtr<class SExtractionProgressionPanel> Panel;
};
