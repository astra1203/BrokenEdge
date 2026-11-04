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
	virtual void NativeDestruct() override;

	/** 小地图：左上角边长（控件单位）、覆盖的世界范围（厘米）与采集分辨率。 */
	static constexpr float MiniSize = 230.f;
	static constexpr float MiniWorld = 8000.f;

protected:
	// 在控件树之上绘制战斗反馈：命中标记、受击方向弧、击倒/击杀提示。
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	                          FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style,
	                          bool bParentEnabled) const override;

private:
	/** Refresh 时记下的相机位置与朝向，受击方向按它换算到屏幕。 */
	FVector ViewEye = FVector::ZeroVector;
	float ViewYaw = 0;
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
	// 以下为分栏 HUD 的可选字段；缺失时对应信息仍写回上面的整段文本。
	/** 对局剩余时间（大字）。 */
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UTextBlock> RaidTimer;
	/** 地图与存活人数。 */
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UTextBlock> RaidInfo;
	/** 当前生命值（大字）。 */
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UTextBlock> HealthText;
	/** 当前武器名称。 */
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UTextBlock> WeaponName;
	/** 弹匣内子弹数（大字）。 */
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UTextBlock> AmmoText;
	/** 撤离倒计时卡片；没有撤离进度时整张收起。 */
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UWidget> ExtractionCard;
	/** 右侧中间的按键提示。 */
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UTextBlock> KeyHints;
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
	// 小地图：从烘焙的整图俯视地图（地图定义 OverviewTexture）里截取玩家周围一块，北朝上。
	void UpdateMinimap(const class APawn* Pawn);
	void PaintMinimap(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const;
	UPROPERTY(Transient)
	TObjectPtr<class UTexture2D> MiniOverview;
	FVector2D MiniMin = FVector2D::ZeroVector, MiniMax = FVector2D::ZeroVector;
	bool bMiniLoaded = false, bMiniActive = false;
	FVector MiniCenter = FVector::ZeroVector;
};
