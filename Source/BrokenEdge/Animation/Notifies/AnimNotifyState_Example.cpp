#include "AnimNotifyState_Example.h"

#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogAnimNotifyStateExample, Log, All);

void UAnimNotifyState_Example::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (!IsValid(MeshComp))
	{
		return;
	}

	UE_LOG(LogAnimNotifyStateExample, Log, TEXT("[Begin] Owner=%s Animation=%s TotalDuration=%.3f"),
		*GetNameSafe(MeshComp->GetOwner()), *GetNameSafe(Animation), TotalDuration);
	// 区间开始：适合通知角色开启攻击检测等逻辑。
}

void UAnimNotifyState_Example::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);

	if (!IsValid(MeshComp))
	{
		return;
	}

	UWorld* World = MeshComp->GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Location = !SocketName.IsNone() && MeshComp->DoesSocketExist(SocketName)
		? MeshComp->GetSocketLocation(SocketName)
		: MeshComp->GetComponentLocation();

	// 仅绘制当前帧；后续 Tick 重新绘制，结束后无需清理持久调试图形。
	// 这是可视化示例，不会执行碰撞查询或造成伤害。
	DrawDebugSphere(World, Location, FMath::Max(DebugRadius, 1.0f), 16, DebugColor, false, -1.0f, 0, 1.5f);

	// Tick 用 Verbose 避免默认刷屏；FrameDeltaTime 是此次更新的时间间隔。
	UE_LOG(LogAnimNotifyStateExample, Verbose, TEXT("[Tick] Owner=%s DeltaTime=%.4f"),
		*GetNameSafe(MeshComp->GetOwner()), FrameDeltaTime);
}

void UAnimNotifyState_Example::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (!IsValid(MeshComp))
	{
		return;
	}

	UE_LOG(LogAnimNotifyStateExample, Log, TEXT("[End] Owner=%s Animation=%s"),
		*GetNameSafe(MeshComp->GetOwner()), *GetNameSafe(Animation));
	// 区间结束：适合通知角色关闭攻击检测等逻辑。
}

FString UAnimNotifyState_Example::GetNotifyName_Implementation() const
{
	return TEXT("Example: Notify State");
}
