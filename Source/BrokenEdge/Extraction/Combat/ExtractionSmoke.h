#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionSmoke.generated.h"
// 烟雾 Actor 复制半径和到期时间，球体只提供视觉表现，不阻挡物理碰撞或导航。
// AI 视线和屏幕烟雾效果通过 Blocks 的几何查询判定，不能用渲染材质替代服务器遮挡规则。
// 视觉上有两套互斥后端：若干压扁球体叠成一团，或者多层 Niagara 云团错位叠放。

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
	// 体积表现改用 Niagara 云团；缺资产或关掉开关时退回多层球体。
	UPROPERTY(EditDefaultsOnly, Category = "Smoke|Visual")
	TObjectPtr<class UNiagaraSystem> CloudSystem = nullptr;
	// 云团是一组错位叠放的 Niagara 组件，不是一个。
	// P_Clouds 单张云的 alpha 只有两三成，铺满 4.5 米后就是一片看得穿的雾；
	// 叠 n 层后不透明度按 1-(1-a)^n 累积，才读得出"烟幕"而不是"雾"。
	// 每层再错开位置和朝向，把单张云里精灵之间的空隙填掉，消掉"散"的观感。
	UPROPERTY(VisibleAnywhere, Category = "Smoke|Visual")
	TArray<TObjectPtr<class UNiagaraComponent>> CloudFxLayers;
	// 云团叠层上限 = 布局表长度。构造时按这个数量建组件，运行时用下面的开关裁剪。
	// 16 层是"浓到接近实心"的量级：单层 alpha 约 0.22，1-(1-0.22)^16 ≈ 0.98，
	// 漏光从 9 层的 10% 压到 2%。再往上加收益迅速衰减，代价是线性的粒子开销。
	static constexpr int32 CloudLayerMax = 16;
	// 实际启用的层数：1 层能看出薄，16 层接近实心。性能吃紧就往下调。
	// 默认值由 ExtractionRules::SmokeCloudLayers 在 BeginPlay 里覆盖。
	UPROPERTY(EditDefaultsOnly, Category = "Smoke|Visual")
	int32 CloudLayerCount = CloudLayerMax;
	// 保留开关：关掉就是旧的多层球体，方便对照、回滚和排查性能问题。
	UPROPERTY(EditDefaultsOnly, Category = "Smoke|Visual")
	bool bUseCloudFx = true;
	// 云团系统自身的半径（厘米）。缩放按 Radius / 这个值推算。
	// P_Clouds 的 FixedBounds 是 ±100，所以这里取 100；换系统时按它的包围盒改。
	UPROPERTY(EditDefaultsOnly, Category = "Smoke|Visual")
	float CloudSystemRadius = 100.f;
	// 视觉半径相对遮挡半径的倍率（ExtractionRules::SmokeCloudSizeScale）。
	// 调小就是"烟比判定范围小一圈"，看着不糊脸，但判定仍是完整的。
	UPROPERTY(EditDefaultsOnly, Category = "Smoke|Visual")
	float CloudSizeScale = 1.f;
	// 垂直尺度倍率（ExtractionRules::SmokeCloudAspect）：1 = 正球，
	// 0.62 = 贴地扁团，4 = 竖直拉高到 4 倍的烟柱。
	// Blocks/Haze 的几何判定读同一个值，表现和判定不会走偏。
	UPROPERTY(EditDefaultsOnly, Category = "Smoke|Visual")
	float CloudAspect = 4.f;
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
	// 基准是否已经采到。不能在 BeginPlay 之外假设它有效：Refresh 会被复制回调
	// 和验收脚本先于 BeginPlay 调用，那时基准还是零向量，正常出生点会被误判成漂移。
	bool bSpawnCaptured = false;
	bool bDriftReported = false;
	// 云团是否接管了表现。接管时球体全部隐藏，避免两层半透明叠在一起糊成白雾。
	bool bCloudFxActive = false;
	// 进烟渐隐按平滑步进计算，靠近烟团边缘时只轻微发灰。
	void ApplyLook();
	// 球体与云团二选一：决定这团烟用哪套表现，只在 BeginPlay 和半径变化时调用。
	void ApplyVisualBackend();
	// 按本地时间轴算剩余寿命。不能拿 ExpiresAt 去减服务器时钟：
	// 两者不同源，客户端时钟超前时剩余寿命会算成负数，烟一出生就全透明。
	float RemainingSeconds(float Age) const;
};
