#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionSmoke.generated.h"
// 烟雾 Actor 复制半径和到期时间，球体只提供视觉表现，不阻挡物理碰撞或导航。
// AI 视线和屏幕烟雾效果通过 Blocks 的几何查询判定，不能用渲染材质替代服务器遮挡规则。

UCLASS()
class BROKENEDGE_API AExtractionSmoke : public AActor
{
	GENERATED_BODY()

public:
	// 创建无碰撞球体并保持网络相关性，让烟雾状态可供各端查询。
	AExtractionSmoke();
	// 服务器从规则设置半径、到期时刻和寿命，各端按当前半径刷新模型。
	virtual void BeginPlay() override;
	// 同步半径和绝对到期时间；客户端按同步服务器时间判断是否仍有效。
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(ReplicatedUsing=Refresh)
	float Radius = 400;
	// 服务器绝对到期时间（秒），寿命销毁消息延迟时仍可据此忽略已过期烟雾。
	UPROPERTY(Replicated)
	double ExpiresAt = 0;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UStaticMeshComponent> Cloud;
	// 检测线段到有效烟雾球心的最短距离是否小于半径；起终点相同可用于判断相机是否在烟内。
	static bool Blocks(const UWorld* World, const FVector& Start, const FVector& End);
	// 基础球半径为 50 厘米，因此用 Radius/50 缩放；这是复制半径的表现回调。
	UFUNCTION()
	void Refresh();
};
