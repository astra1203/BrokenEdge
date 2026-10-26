#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionKeyRoom.generated.h"
// 一次性钥匙房：服务器验证背包版本并消耗钥匙，先发布解锁，再在开门时刻取消通行阻挡。
// 门动画是复制时刻驱动的表现；宝箱搜索还会检查 Open，不能仅看门的可见位置。

UCLASS()
class BROKENEDGE_API AExtractionKeyRoom : public AActor
{
	GENERATED_BODY()

public:
	// 创建权威阻挡盒与无碰撞门模型；门不改导航烘焙，是否可通行由阻挡盒控制。
	AExtractionKeyRoom();
	// 记录关门相对位置并刷新表现；服务器生成一次机密宝箱和文件来源批次，供撤离任务核验。
	virtual void BeginPlay() override;
	// 使用同步服务器时间插值门模型；仅服务器在到时后发布 Open 并移除阻挡。
	virtual void Tick(float DeltaSeconds) override;
	// 同步解锁、完全打开、开门截止时间和宝箱引用；文件来源批次不向客户端公开。
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UBoxComponent> Barrier;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UStaticMeshComponent> DoorMesh;
	UPROPERTY(EditAnywhere)
	FVector Slide = FVector(180, 0, 0);
	UPROPERTY(EditAnywhere)
	FVector LootOffset = FVector(0, 400, 25);
	// Unlocked 表示钥匙已消耗且开门开始；Open 表示动作完成并可通行，二者不能混用。
	UPROPERTY(ReplicatedUsing=OnRep_Door)
	bool Unlocked = false;
	UPROPERTY(ReplicatedUsing=OnRep_Door)
	bool Open = false;
	UPROPERTY(Replicated)
	double OpenAt = 0;
	UPROPERTY(Replicated)
	TObjectPtr<class AExtractionLootActor> Cache;
	// 本门生成的文件来源批次，服务器结算时汇总到本局任务证据。
	UPROPERTY()
	TArray<FGuid> FileLots;
	// 要求同世界、两米内且视线无遮挡；射线命中门本身允许交互。
	bool CanReach(class ABrokenEdgeCharacter* Character) const;
	// 服务器验证行动、存储、动作互斥和背包版本，再在副本中移除钥匙并校验。
	// 成功后递增版本并设定 0.8 秒开门时刻；返回值是反馈文本，成功时也不是空字符串。
	FString Unlock(class UExtractionInteractionComponent* Interaction, int32 InventoryRevision);
	// 给出门前方的世界参考位置，用于接近该门的调用者，不代表已经通过距离检查。
	FVector Approach() const;

private:
	FVector ClosedLocation;
	// 以 Open 控制阻挡，并在已打开时直接对齐最终模型位置，支持晚收到状态的客户端。
	UFUNCTION()
	void OnRep_Door();
};
