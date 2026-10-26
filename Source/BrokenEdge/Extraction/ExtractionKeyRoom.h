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
	virtual void OnConstruction(const FTransform& Transform) override;
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
	/**
	 * 交互判定碰撞盒。角色进入这个盒子才算"在门边"，HUD 才会提示按 F 开门。
	 * QueryOnly + 只对 Pawn 产生 Overlap，不影响移动、子弹和导航。
	 */
	UPROPERTY(VisibleAnywhere, Category="KeyRoom|Interaction")
	TObjectPtr<class UBoxComponent> InteractionZone;
	/** 交互盒相对门洞中心的偏移（厘米，本地坐标）。本地 -Y 为门外一侧。 */
	UPROPERTY(EditAnywhere, Category="KeyRoom|Interaction")
	FVector InteractionZoneOffset = FVector::ZeroVector;
	/** 在门洞尺寸基础上向宽/深/高外扩的量（厘米），决定玩家离门多远能按 F。 */
	UPROPERTY(EditAnywhere, Category="KeyRoom|Interaction")
	FVector InteractionZonePadding = FVector(65, 110, 45);
	/** 门扇显示尺寸（厘米，宽×厚×高）。留零表示与 Barrier 门洞完全一致。 */
	UPROPERTY(EditAnywhere, Category="KeyRoom|Door")
	FVector DoorSize = FVector::ZeroVector;
	/** 门扇相对门洞中心的额外偏移（厘米，本地坐标），用于避让门框或墙面。 */
	UPROPERTY(EditAnywhere, Category="KeyRoom|Door")
	FVector DoorMeshOffset = FVector::ZeroVector;
	/** 关闭时门扇是否作为实体碰撞体阻挡子弹、视线与角色。 */
	UPROPERTY(EditAnywhere, Category="KeyRoom|Door")
	bool bDoorBlocks = true;
	/** 没有手工指定模型时，是否套用默认门扇模型。 */
	UPROPERTY(EditAnywhere, Category="KeyRoom|Door")
	bool bUseDefaultDoorMesh = true;
	/** 是否把门扇等比缩放到门洞宽高；关掉后完全使用模型自身比例。 */
	UPROPERTY(EditAnywhere, Category="KeyRoom|Door")
	bool bFitDoorMesh = true;
	UPROPERTY(EditAnywhere)
	FVector Slide = FVector(180, 0, 0);
	UPROPERTY(EditAnywhere)
	FVector LootOffset = FVector(0, 400, 25);
	/** 房号：与 UExtractionItemDefinition::KeyRoomId 对应；留空表示沿用旧的一次性钥匙。 */
	UPROPERTY(EditAnywhere, Category="KeyRoom")
	FName RequiredKey;
	/** 交互提示里显示的房名，例如"医疗室"。 */
	UPROPERTY(EditAnywhere, Category="KeyRoom")
	FText RoomLabel;
	/** 0=基础 1=进阶 2=高级 3=顶级；决定储藏箱里的战利品数量。 */
	UPROPERTY(EditAnywhere, Category="KeyRoom", meta=(ClampMin="0", ClampMax="3"))
	int32 LootTier = 0;
	/** 房间里那个箱子的显示名。 */
	UPROPERTY(EditAnywhere, Category="KeyRoom")
	FString CacheLabel = TEXT("机密储藏箱");
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
	/** 角色是否站在这扇门的交互碰撞盒内（先查重叠，再按盒体几何兜底）。 */
	bool IsInInteractionRange(const AActor* Other) const;
	// 服务器验证行动、存储、动作互斥和背包版本，再在副本中移除钥匙并校验。
	// 成功后递增版本并设定 0.8 秒开门时刻；返回值是反馈文本，成功时也不是空字符串。
	FString Unlock(class UExtractionInteractionComponent* Interaction, int32 InventoryRevision);
	// 给出门前方的世界参考位置，用于接近该门的调用者，不代表已经通过距离检查。
	FVector Approach() const;
	/** 交互提示文本："医疗室 · 需要医疗室钥匙卡"。 */
	FString PromptText() const;
	/** 当前房间要求的钥匙卡显示名（BeginPlay 时按房号解析）。 */
	UPROPERTY()
	FText RequiredCardLabel;

private:
	FVector ClosedLocation;
	/** 按门洞尺寸缩放并对齐门扇模型；模型为空时套用默认门扇。 */
	void FitDoorMesh();
	/** 按门洞尺寸和 InteractionZonePadding 重算交互碰撞盒。 */
	void RefreshInteractionZone();
	/** 同步门洞与门扇的碰撞体状态。 */
	void ApplyDoorState(bool bOpen);
	// 以 Open 控制阻挡，并在已打开时直接对齐最终模型位置，支持晚收到状态的客户端。
	UFUNCTION()
	void OnRep_Door();
};
