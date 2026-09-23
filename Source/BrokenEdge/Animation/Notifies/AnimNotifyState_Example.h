#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_Example.generated.h"

/** 区间通知：演示 Begin -> Tick -> End，在持续期间绘制跟随骨骼的调试球。 */
UCLASS(meta = (DisplayName = "Example: Notify State"))
class BROKENEDGE_API UAnimNotifyState_Example : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float TotalDuration, const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;

	// 可填写 socket 或骨骼名称；留空/名称不存在时使用网格组件位置。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Example")
	FName SocketName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Example", meta = (ClampMin = "1.0", Units = "cm"))
	float DebugRadius = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Example")
	FColor DebugColor = FColor::Green;

	// 多个角色播放同一个动画时可能共用此通知对象。
	// 此处只保存配置；角色指针、计时器、命中列表等运行状态应放到角色或组件中。
};
