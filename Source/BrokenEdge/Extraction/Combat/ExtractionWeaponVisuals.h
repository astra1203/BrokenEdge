#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ExtractionWeaponVisuals.generated.h"
// 武器附件的本地视觉装配：读取已复制的配置，分别创建世界模型与本人第一人称模型。
// 专服跳过外观创建；这些组件不参与伤害、库存或持久化，销毁表现不会消耗附件物品。

UCLASS()
class BROKENEDGE_API UExtractionWeaponVisuals : public UActorComponent
{
	GENERATED_BODY()
public:
	// 逐帧检查配置与第一人称父组件是否变化，仅发生变化时重建。
	UExtractionWeaponVisuals();
	// 比较配件列表、装配布局和视角父组件；父件先于子件创建，随后按视角更新可见性。
	virtual void TickComponent(float Delta, ELevelTick Kind, FActorComponentTickFunction* Tick) override;
	// 武器结束生命周期时释放全部动态附件，再调用父类清理。
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	TArray<FName> Built;
	uint32 BuiltLayout = 0;
	UPROPERTY(Transient) TArray<TObjectPtr<class UStaticMeshComponent>> WorldParts;
	UPROPERTY(Transient) TArray<TObjectPtr<class UStaticMeshComponent>> ViewParts;
	// 第一人称枪械组件可能重建，弱引用用于检测父对象变化，不能永久缓存旧组件。
	TWeakObjectPtr<class USkeletalMeshComponent> ViewParent;
	// 销毁已生成的两套附件组件并清空缓存，使下一次刷新能按新父对象重建。
	void Clear();
};
