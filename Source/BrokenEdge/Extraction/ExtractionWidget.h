#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ExtractionWidget.generated.h"

UCLASS(Abstract, Blueprintable)
class BROKENEDGE_API UExtractionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 控件树由 WBP_ExtractionHUD 提供；这里只绑定交互。
	virtual void NativeOnInitialized() override;
	// 读取 GameState、PlayerState 和本人账号快照更新倒计时、撤离点、任务、生命与武器信息。
	// 使用同步服务器时间计算剩余秒数，进度条到零本身不会触发撤离结算。
	void Refresh();

private:
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<class UTextBlock> Status;
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<class UBorder> SmokeHaze;
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<class UTextBlock> Vitals;
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<class UTextBlock> WeaponStatus;
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<class UTextBlock> ExitStatus;
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<class UTextBlock> Compass;
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<class UTextBlock> ExtractionCountdown;
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<class UProgressBar> ExtractionProgress;
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<class UProgressBar> HealthProgress;
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<class UButton> Previous;
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<class UButton> Next;
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<class UButton> Leave;
	// 请求服务器选择上一位合法观战队友。
	UFUNCTION()
	void PreviousClicked();
	// 请求服务器选择下一位合法观战队友。
	UFUNCTION()
	void NextClicked();
	// 按钮只提交离开请求，会话清理由服务器确认后的客户端链路处理。
	UFUNCTION()
	void LeaveClicked();
};
