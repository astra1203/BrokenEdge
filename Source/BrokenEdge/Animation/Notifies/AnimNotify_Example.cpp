#include "AnimNotify_Example.h"

#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogAnimNotifyExample, Log, All);

void UAnimNotify_Example::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!IsValid(MeshComp))
	{
		return;
	}

	// MeshComp 是此次播放动画的网格；通过它获取对应角色。
	AActor* Owner = MeshComp->GetOwner();
	UE_LOG(LogAnimNotifyExample, Log, TEXT("[Notify] Owner=%s Animation=%s Message=%s"),
		*GetNameSafe(Owner), *GetNameSafe(Animation), *Message);

	// 可以在这里调用角色/组件接口，例如播放脚步声或通知装填完成。
}

FString UAnimNotify_Example::GetNotifyName_Implementation() const
{
	return TEXT("Example: Single Notify");
}
