#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_Example.generated.h"

/** 单次通知：动画播放经过通知点时执行一次，例如脚步声、弹匣装填完成。 */
UCLASS(meta = (DisplayName = "Example: Single Notify"))
class BROKENEDGE_API UAnimNotify_Example : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;

	// 在动画编辑器中选中通知后，可以修改这条消息。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Example")
	FString Message = TEXT("Animation notify fired!");
};
