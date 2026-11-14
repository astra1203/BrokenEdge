#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionItems.h"
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
	// 容器外观（保险箱等）用可摆姿态的网格显示，打开后直接转动门骨骼，不需要动画蓝图。
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UPoseableMeshComponent> ContainerMesh;
	/**
	 * 打开后要转动的门骨骼：键为“网格名:骨骼名”，值为绕 Z 轴的开门角度（度，符号决定朝外开）。
	 * 大保险门铰链在右侧，-100°；小保险门铰链在左侧，+100°。
	 */
	UPROPERTY(EditDefaultsOnly, Category="Extraction|Appearance")
	TMap<FString, float> DoorBones = {
		{TEXT("props_collect_box_546_3P:Bone002"), -100.f},
		{TEXT("props_collect_SmallSafeBox_802_3P:Point001"), 100.f}};
	// 任何玩家第一次打开容器后为 true；复制给所有人，门保持打开。
	UPROPERTY(ReplicatedUsing=OnRep_Opened)
	bool bOpened = false;
	// 服务器：打开容器界面时调用，记录已打开以播放开门。
	void MarkOpened();
	virtual void Tick(float DeltaSeconds) override;
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
	// 静态网格外观（航空箱等），优先于 ContainerAppearance；盖子绕 LidHinge 处的 X 轴掀开。
	UPROPERTY(ReplicatedUsing=OnRep_Appearance)
	TObjectPtr<UStaticMesh> ContainerStatic;
	UPROPERTY(ReplicatedUsing=OnRep_Appearance)
	TObjectPtr<UStaticMesh> ContainerLid;
	UPROPERTY(ReplicatedUsing=OnRep_Appearance)
	FVector LidHinge = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> LidMesh;
	/** 盖子全开角度（度，负值让前沿向上向后翻）。 */
	UPROPERTY(EditDefaultsOnly, Category="Extraction|Appearance")
	float LidOpenDegrees = -105.f;
	// 关联门尚未打开时，交互距离满足也不能搜索该容器。
	UPROPERTY(Replicated)
	TObjectPtr<class AExtractionKeyRoom> AccessDoor;
	/** Filled only once by authority, never by opening the UI. */
	// 权威物品内容不参与 Actor 属性复制，不能把它当作客户端已知的库存。
	const FExtractionContainer& GetContents() const { return Contents; }
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
	friend class UExtractionInventoryComponent;
	UPROPERTY()
	FExtractionContainer Contents;
	// 依据整箱/散落物/枪械切换模型和查询边界；不移动作为根组件的交互盒，以免改变配置点位。
	UFUNCTION()
	void OnRep_Appearance();
	UFUNCTION()
	void OnRep_Opened();
	// 把门骨骼摆到 DoorProgress（0 关 / 1 全开）对应的角度。
	void ApplyDoorPose();
	float DoorProgress = 0.f;
};
