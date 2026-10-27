#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionSmokeGrenade.generated.h"

class AExtractionSmoke;
class UProjectileMovementComponent;
class UStaticMeshComponent;
// 烟雾弹投掷物：服务器用抛物线把烟雾送到落点，静止后等待 FuseSeconds 再生成烟幕。
// 飞行与保险丝计时只在服务器推进，客户端依靠复制的位置和 ExpiresAt 判断是否已起烟。
// 起烟后自毁，因此投掷物本身不进 Blocks 查询，也不提供任何遮挡。

UCLASS()
class BROKENEDGE_API AExtractionSmokeGrenade : public AActor
{
	GENERATED_BODY()

public:
	// 建立弹体网格与投射物移动组件；碰撞只用于检测落地，不阻挡角色。
	AExtractionSmokeGrenade();
	// 服务器开始保险丝计时；客户端记录同步时间用于本地表现。
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	// 只对服务器有意义：服务器绝对起烟时刻，0 表示尚未落地。
	UPROPERTY(Replicated)
	double FuseAt = 0;
	// 起烟后生成的烟幕参数由投掷者在 Abilities 中设定，避免投掷物再引用规则对象。
	void SetSmokeTuning(float InRadius, float InSeconds, float InFuseSeconds)
	{
		Radius = InRadius;
		Seconds = InSeconds;
		FuseSeconds = InFuseSeconds;
	}
	// 落点被墙壁或地面挡住时取命中点，未命中则用预定落点；只在服务器调用。
	void Launch(const FVector& From, const FVector& To, float Speed);

protected:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Shell;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProjectileMovementComponent> Flight;
	// 投射物命中回调：第一次接触地面或墙壁时固定位置并开始保险丝。
	virtual void NotifyHit(UPrimitiveComponent* Component, AActor* Other, UPrimitiveComponent* OtherComponent,
	                       bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse,
	                       const FHitResult& Hit) override;

private:
	// 落地后固定并使用命中点作为烟幕中心，避免弹体从斜坡继续下滑。
	void Settle(const FVector& Location);
	// 保险丝到期：生成烟幕并销毁弹体。
	void Ignite();
	// 飞行超时上限：投掷物没撞到任何东西时也必须起烟，否则玩家看到的是"什么都没有"。
	// 正常抛物线 2 秒内落地，5 秒是明显异常才走这条路径。
	static constexpr float MaxAirborneSeconds = 5.f;
	float Radius = 400;
	float Seconds = 15;
	float FuseSeconds = 1.5f;
	bool bSettled = false;
	bool bIgnited = false;
	FTimerHandle FuseTimer;
};
