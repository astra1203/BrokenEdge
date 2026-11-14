#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionStimDart.generated.h"
// 激素枪飞镖：服务器按官方弹道（BulletId 2007）逐子步推进并扫掠命中，命中存活队友才施加激素效果。
// 客户端只看复制的位置；命中敌人、墙体或飞满射程都只是浪费这一发，不给任何人加效果。

class ABrokenEdgeCharacter;

UCLASS()
class BROKENEDGE_API AExtractionStimDart : public AActor
{
	GENERATED_BODY()

public:
	AExtractionStimDart();
	// 服务器调用：Shooter 是开枪的蜂医，Direction 为准星方向；速度、射程、半径来自 UExtractionRules。
	void Launch(ABrokenEdgeCharacter* Shooter, const FVector& Direction);
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UStaticMeshComponent> Body;
	TWeakObjectPtr<ABrokenEdgeCharacter> ShooterCharacter;
	FVector Velocity = FVector::ZeroVector;
	float Distance = 0, Accumulator = 0;
	// 命中检测：只有存活的友方（含自己以外的队友）才算有效目标。
	void Strike(const FHitResult& Hit);
};
