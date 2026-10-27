#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionSmoke.generated.h"
// 烟雾 Actor 复制半径和到期时间，球体只提供视觉表现，不阻挡物理碰撞或导航。
// AI 视线和屏幕烟雾效果通过 Blocks 的几何查询判定，不能用渲染材质替代服务器遮挡规则。
// 视觉上用若干压扁球体叠成一团有体积的烟，单球加半透明贴图会只剩一层薄壳、显得很淡。

UCLASS()
class BROKENEDGE_API AExtractionSmoke : public AActor
{
	GENERATED_BODY()

public:
	// 创建无碰撞烟团并保持网络相关性，让烟雾状态可供各端查询。
	AExtractionSmoke();
	// 服务器从规则设置半径、到期时刻和寿命，各端按当前半径刷新模型。
	virtual void BeginPlay() override;
	// 同步半径和绝对到期时间；客户端按同步服务器时间判断是否仍有效。
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	// 每帧推进淡入、鼓胀和淡出；外观完全由本地时间推导，不再需要额外的复制字段。
	virtual void Tick(float Delta) override;
	UPROPERTY(ReplicatedUsing=Refresh)
	float Radius = 400;
	// 服务器绝对到期时间（秒），寿命销毁消息延迟时仍可据此忽略已过期烟雾。
	UPROPERTY(Replicated)
	double ExpiresAt = 0;
	// 主烟团；其余 Puffs 共用同一材质，构成叠层体积。
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UStaticMeshComponent> Cloud;
	// 叠加烟团：每层有独立的固定偏移与尺寸，整体一起鼓胀和淡出。
	UPROPERTY(VisibleAnywhere)
	TArray<TObjectPtr<class UStaticMeshComponent>> Puffs;
	// 由投掷物在生成后指定半径与存续秒数；留空则回退到规则默认值。
	void SetTuning(float InRadius, float InSeconds);
	// 检测线段到有效烟雾球心的最短距离是否小于半径；起终点相同可用于判断相机是否在烟内。
	static bool Blocks(const UWorld* World, const FVector& Start, const FVector& End);
	// 相机在烟内时返回 0..1 的相对深度，用于全屏蒙版的淡入淡出；不在烟内返回 0。
	static float Haze(const UWorld* World, const FVector& Eye);
	// 主烟团当前写入材质的不透明度，供验收测试确认烟雾真的可见（而不是逻辑上存在、画面上是空气）。
	float VisualAlpha() const { return AppliedAlpha; }
	// 基础球半径为 50 厘米，因此用 Radius/50 缩放；这是复制半径的表现回调。
	UFUNCTION()
	void Refresh();

private:
	// 外观时间轴：本地首次渲染时记下起点，之后按淡入、保持、淡出三段插值。
	float BornAt = -1.f;
	float LifetimeSeconds = 15;
	// 淡入与淡出秒数；淡出时长受寿命上限约束，短寿命时按比例压缩。
	float FadeInSeconds = .5f;
	float FadeOutSeconds = 2.2f;
	// 上一帧写入的不透明度，避免重复设置参数。
	float AppliedAlpha = -1.f;
	// 诊断日志的上次打印时刻（Age 秒）；-1 表示本团还没打过一行。
	float LastLogAge = -1.f;
	// 生成位置。表现代码若在根组件上写了位置，Actor 会被搬走 —— 这类 bug 表现为
	// "烟完全没有"，但组件、材质、alpha 全都正常，极难察觉，所以在这里做一次自检。
	FVector SpawnLocation = FVector::ZeroVector;
	bool bDriftReported = false;
	// 进烟渐隐按平滑步进计算，靠近烟团边缘时只轻微发灰。
	void ApplyLook();
	// 按本地时间轴算剩余寿命。不能拿 ExpiresAt 去减服务器时钟：
	// 两者不同源，客户端时钟超前时剩余寿命会算成负数，烟一出生就全透明。
	float RemainingSeconds(float Age) const;
};
