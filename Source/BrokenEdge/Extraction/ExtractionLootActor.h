#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionItems.h"
#include "ExtractionLootActor.generated.h"
// 战利品容器 Actor 只复制外观、标识与是否为空；完整 Contents 留在服务器。
// 每位查看者通过 InteractionComponent 获得自己的搜索视图，打开界面不会重新随机生成物资。

/** Contents stay on the server. Each viewer receives a separately filtered view. */
UCLASS(Blueprintable)
class BROKENEDGE_API AExtractionLootActor : public AActor
{
	GENERATED_BODY()

public:
	// 分离交互盒、容器阻挡盒和可见模型；模型不承担物品规则，碰撞负责视线与通行查询。
	AExtractionLootActor();
	/** Resolve the configured authority class; never silently substitute the native class. */
	static TSubclassOf<AExtractionLootActor> ResolveSpawnClass(UWorld* World);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Extraction|Appearance")
	TObjectPtr<UStaticMesh> DefaultContainerMesh;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Extraction|Appearance", meta=(ClampMin="1"))
	float ContainerSize = 80.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Extraction|Appearance", meta=(ClampMin="1"))
	float LooseSize = 34.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Extraction|Appearance")
	FVector ContainerInteractionExtent = FVector(40, 30, 30);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Extraction|Appearance")
	FVector LooseInteractionExtent = FVector(45, 25, 20);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Extraction|Appearance")
	FVector VisualOffset = FVector(0, 0, -20);
	virtual void OnConstruction(const FTransform& Transform) override;
	// 各端刷新外观，服务器初始化容器 ID；只有 SeedContents 开启且内容为空时才生成默认物资。
	virtual void BeginPlay() override;
	// 复制公开外观和门关联，不注册完整 Contents，避免泄露未搜索的具体物品。
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UBoxComponent> InteractionBounds;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UBoxComponent> ContainerBounds;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class USkeletalMeshComponent> WeaponMesh;
	UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_Appearance)
	bool Loose = false;
	UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_Appearance)
	FName LooseDefinition;
	UPROPERTY(EditAnywhere)
	int32 LooseQuantity = 1;
	UPROPERTY(EditAnywhere, Replicated)
	FString Label = TEXT("物资箱");
	UPROPERTY(Replicated)
	bool Empty = false;
	UPROPERTY(Replicated)
	FGuid ContainerId;
	UPROPERTY(Replicated)
	FName SourcePointId;
	UPROPERTY(ReplicatedUsing=OnRep_Appearance)
	TObjectPtr<class USkeletalMesh> ContainerAppearance;
	// 关联门尚未打开时，交互距离满足也不能搜索该容器。
	UPROPERTY(Replicated)
	TObjectPtr<class AExtractionKeyRoom> AccessDoor;
	/** Filled only once by authority, never by opening the UI. */
	// 权威物品内容不参与 Actor 属性复制，不能把它当作客户端已知的库存。
	UPROPERTY()
	FExtractionContainer Contents;
	// 仅控制首次生成配方与默认落地调整；布局导入和玩家掉落会关闭它。
	UPROPERTY(EditAnywhere)
	bool SeedContents = true;
	/** Bounded equipment supply fixtures; zero keeps the existing loot recipe. */
	UPROPERTY(EditAnywhere)
	int32 EquipmentTier = 0;
	// 服务器同步空状态与容器 ID；空散落物延后销毁，旧 RPC 仍须重新校验对象与会话。
	void Changed();
	// 仅服务器替换容器内容并通知公开状态更新；搜索视图由查看者另行刷新。
	void SetContents(const FExtractionContainer& NewContents);
	// 返回容器外观中心的世界位置或交互盒位置，供距离和视线判断使用。
	FVector InteractionPoint() const;
	// 延迟生成运行时掉落容器，关闭自动种子物资以免额外产出；返回空表示生成失败。
	static AExtractionLootActor* SpawnContainer(UWorld* World, const FVector& Position, bool bLoose,
	                                            FName Definition = NAME_None);

private:
	// 依据整箱/散落物/枪械切换模型和查询边界；不移动作为根组件的交互盒，以免改变配置点位。
	UFUNCTION()
	void OnRep_Appearance();
};
