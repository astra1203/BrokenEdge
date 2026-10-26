#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionRegionStream.generated.h"
// 区域流送管理器：服务器常驻该区域，客户端按玩家、观战目标及相机到区域的距离异步加载。
// 加载进内存和关卡可见是两个阶段；进入区域前还需确认该客户端已报告可见，防止踩进未加载碰撞。

/** A bounded client streaming region. Gameplay actors and server navigation remain resident. */
UCLASS()
class BROKENEDGE_API AExtractionRegionStream : public AActor
{
	GENERATED_BODY()
public:
	// 开启逐帧管理并创建根组件；各世界实例独立管理本地流送，不通过复制 Actor 请求加载。
	AExtractionRegionStream();
	// 客户端采用预加载/卸载两档距离与延迟形成滞回，避免边界来回移动频繁加载。
	// 提交异步加载可见请求后记录状态变化，再在服务器和本地执行边界保护。
	virtual void Tick(float DeltaSeconds) override;
	// 清空 Pawn 安全位置缓存，避免跨世界保留失效角色引用。
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	// 查找世界中的首个区域管理器，没配置时返回空，调用方可按无区域门槛处理。
	static AExtractionRegionStream* Find(const UWorld* World);
	// 仅关卡已可见才算就绪，IsLoaded 为真仍可能尚未挂入世界。
	bool IsReady() const;
	// 报告资源已加载状态，供统计和诊断使用，不等同于可安全进入。
	bool IsLoaded() const;
	bool IsRequested() const { return bRequested; }
	// 远离预加载范围时无需等待该区域；附近视角必须等到关卡可见。
	bool IsViewReady(const FVector& Location) const;
	bool IsBlocked() const { return bBlocked; }
	int32 GetLoads() const { return Loads; }
	int32 GetUnloads() const { return Unloads; }
	// 仅请求中但尚未可见时显示加载提示；超过 30 秒改为失败提示，不宣称已经取消加载。
	FString GetStatus() const;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName RegionId = TEXT("North");
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UWorld> RegionLevel;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector RegionMin = FVector(-5000, 6000, -5000);
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector RegionMax = FVector(7000, 12000, 8000);
	// 预加载和卸载距离均为厘米，卸载阈值更远并附加秒制延迟，避免频繁抖动。
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float PreloadDistance = 3000;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float UnloadDistance = 5000;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float UnloadDelay = 5;
	// Only used by Development process-driver tests to exercise a stalled local load.
	// 仅开发自动化测试可延迟客户端加载，用于验证区域不可见时的边界保护。
	void SetTestLoadDelay(double Seconds);

private:
	// 按软引用资产名查找当前世界的流送关卡对象，不在此创建或阻塞加载。
	class ULevelStreaming* Streaming() const;
	// 计算点到区域包围盒的水平距离，单位厘米，区域内部水平距离为零。
	float Distance(const FVector& Point) const;
	// 服务器自身就绪后，再检查网络连接记录的客户端可见关卡；本地控制器无连接时不额外等待。
	bool ClientReady(class APlayerController* PC) const;
	// 保存区域外安全位置；客户端未就绪却进入区域时退回该位置并停止移动。
	void ProtectBoundary(class APlayerController* PC, bool bReady);
	// 缓存区域外最后安全位置，弱键不会延长 Pawn 生命周期，Tick 会清理失效键。
	TMap<TWeakObjectPtr<APawn>, FVector> SafePositions;
	// 离开卸载范围的起始时刻，-1 表示尚未开始计时；RequestedAt 用于加载提示超时。
	double OutsideSince = -1;
	double RequestedAt = -1;
	double DelayUntil = 0;
	bool bRequested = false;
	bool bWasLoaded = false;
	bool bWasVisible = false;
	bool bBlocked = false;
	int32 Loads = 0;
	int32 Unloads = 0;
};
