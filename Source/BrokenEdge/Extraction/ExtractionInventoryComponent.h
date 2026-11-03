#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ExtractionItems.h"
#include "ExtractionInventoryComponent.generated.h"
// 局内物品的权威容器：玩家挂在 PlayerState 上，守卫可挂在角色上。
// Contents 保存物品实例，武器 Actor 是其场景表现；操作时要同步两者，不能只改变可见枪械。
// 背包内容只复制给拥有者，来源批次用于服务器的消耗、转移和崩溃恢复。

UCLASS()
class BROKENEDGE_API UExtractionInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// 启用组件复制；具体哪些字段复制及发给谁由 GetLifetimeReplicatedProps 指定。
	UExtractionInventoryComponent();
	// 背包 Contents 仅复制给所属客户端，避免把玩家完整库存广播给所有人。
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	// 运行时权威背包快照，Revision 用于拒绝基于旧界面的操作。
	UPROPERTY(ReplicatedUsing=OnRep_Contents)
	FExtractionContainer Contents;
	// 服务器从出战票据中的 Original 初始化携带装备；用世界标识防止同一世界重复发放。
	// 补给放不下时在附近安全地面生成容器，然后同步枪械 Actor 和背包版本。
	void InitializeLoadout();
	/** Called once by raid initialization; returns supplies that must be placed beside the spawn. */
	// 补充 SCAR-H 副武器与 60 发 7.62×51 弹；逐发填补零散堆叠，返回装不下的补给容器。
	static FExtractionContainer GrantStartingSupply(FExtractionContainer& Inventory);
	// 为服务器守卫创建独立 AK、弹药和医疗包，不读取玩家长期档案。
	void InitializeGuard();
	// 刷新备弹并请求尽快网络更新；版本号由修改容器的调用方推进。
	void Changed();
	// 按物品 ID 复用或创建主副武器 Actor，再同步弹药、附件、手持和背负挂点。
	// 先准备所有需要的 Actor，生成失败则销毁本次新建对象并返回 false，让上层撤回库存变更。
	bool SyncWeapons();
	// 结算或掉落前把当前枪械弹匣和槽位写回物品实例；有变化才增加版本。
	void CaptureWeapons();
	// 从背包当前弹种总量刷新旧战斗组件的备弹显示，备弹不再是可独立增减的另一份库存。
	void SyncReserve();
	// 主副武器槽位固定（槽 0 主武器 / 槽 1 副武器），手上拿哪一把单独记录在 bHoldingSecondary。
	// 返回当前应握在手里的槽位；两槽都空时返回 -1。
	int32 HeldSlot() const;
	const FExtractionItem* HeldGun() const { const int32 S = HeldSlot(); return S < 0 ? nullptr : Contents.InSlot(S); }
	// 服务器：手持槽改为 Slot，由 SyncWeapons 重新分配手持/背负的武器 Actor。
	void SetHeldSlot(int32 Slot);
	// true 表示当前手持副武器槽；复制给拥有者，供 1/2 键判断是否需要切换。
	UPROPERTY(ReplicatedUsing=OnRep_Contents)
	bool bHoldingSecondary = false;
	// 按弹匣空位和同弹种备弹决定装填数量；来源批次随子弹从背包转入弹匣，返回实际装入发数。
	int32 Reload(class AWeapon* Weapon);
	// 服务器接受射击后记录弹匣余量，并从弹匣来源中消耗一发，保持数量与来源守恒。
	void RecordShot(class AWeapon* Weapon);
	// 生成地面容器成功后才清空背包；bDropped 阻止死亡、离局等多个入口重复掉落。
	void DropAll(const FVector& Position);
	// 结算后的清理路径：标记已处理并清空物品和武器，防止角色销毁时再次掉落已入库资产。
	void ClearSettled();
	// 玩家通过 PlayerState 找当前 Pawn；守卫组件直接取所属角色，统一后续装备操作入口。
	class ABrokenEdgeCharacter* Character() const;
	bool WasDropped() const { return bDropped; }

private:
	bool bInitialized = false;
	TWeakObjectPtr<UWorld> InitializedWorld;
	// 整体掉落或结算清理完成标志；同一批物品只能转移一次。
	bool bDropped = false;
	// 客户端收到背包快照后更新备弹显示；复制回调不执行入库或掉落。
	UFUNCTION()
	void OnRep_Contents();
};
