#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ExtractionItems.h"
#include "ExtractionInteractionComponent.generated.h"
// 交互把本地选中目标与服务器物品事务分开：焦点用于提示，真正操作仍需服务器重新检查距离、遮挡和版本。
// 搜索结果按玩家独立保存，LootView 是已脱敏的容器视图，不直接广播服务器完整 Contents。

class AExtractionLootActor;
class UExtractionInventoryComponent;
class ABrokenEdgeCharacter;

/**
 * 撤离模式的物品交互组件，负责背包开关、拾取与转移、逐件搜索和医疗物品使用。
 * 本地控制的角色更新交互焦点；服务器校验物品操作并推进搜索、治疗计时。
 * 标记为 Replicated 的交互状态仅复制给所属客户端，容器视图会隐藏尚未搜索的物品信息。
 */
UCLASS()
class BROKENEDGE_API UExtractionInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 默认启用组件复制，并以 0.1 秒间隔更新交互。 */
	UExtractionInteractionComponent();
	/** 注册仅向所属客户端复制的背包、搜索和治疗状态。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	/** 更新本地焦点；服务器检查交互有效性、推进搜索，并完成或中断治疗。 */
	virtual void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* Function) override;
	/** 背包界面是否打开；直接拾取地面散落物品时不会打开界面。 */
	UPROPERTY(Replicated)
	bool InventoryOpen = false;
	/** 最近接受的开关会话编号；Open 仅接受更大的编号，容器操作需匹配当前会话。 */
	UPROPERTY(Replicated)
	uint32 Session = 0;
	/** 当前打开的战利品容器；仅查看自身背包时为空。 */
	UPROPERTY(Replicated)
	TObjectPtr<AExtractionLootActor> Loot;
	/** 发给所属客户端的容器快照，未揭示物品的标识和内容信息会被清空。 */
	UPROPERTY(Replicated)
	FExtractionContainer LootView;
	/** 当前物品搜索完成的世界时间（秒）；0 表示没有正在进行的搜索。 */
	UPROPERTY(Replicated)
	double SearchEnd = 0;
	/** 当前治疗完成的世界时间（秒）；0 表示没有正在进行的治疗。 */
	UPROPERTY(Replicated)
	double MedicalEnd = 0;
	/** 本次治疗的持续时间（秒），开始使用时从物品定义读取，最小为 0.1 秒。 */
	UPROPERTY(Replicated)
	double MedicalDuration = 3;
	/** 正在使用的医疗物品实例 ID；未治疗时无效，用于阻止操作该物品。 */
	UPROPERTY(Replicated)
	FGuid MedicalItem;
	/** 本地选中的可交互战利品，不参与网络复制或持久化。 */
	UPROPERTY(Transient)
	TObjectPtr<AExtractionLootActor> Focus;
	/** 本地选中的未解锁钥匙房门，与战利品焦点互斥，不参与复制或持久化。 */
	UPROPERTY(Transient)
	TObjectPtr<class AExtractionKeyRoom> DoorFocus;
	/** 交互反馈文本，不参与网络复制；直接拾取时记录操作错误。 */
	FString Message;
	/** 当前世界是否使用撤离模式的 GameState。 */
	bool IsEnabled() const;
	/** 角色是否允许行动，且拥有尚未整体掉落的背包。 */
	bool CanAct() const;
	/** 检查容器有效性、关联门是否打开、200 厘米交互距离及视线遮挡。 */
	bool CanReach(const AExtractionLootActor* Target) const;
	/** 获取玩家状态中的背包；没有对应玩家状态的守卫使用自身背包，可返回空。 */
	UExtractionInventoryComponent* Inventory() const;
	/** 将组件所有者转换为 BrokenEdge 角色，类型不符时返回空。 */
	ABrokenEdgeCharacter* Character() const;
	/**
	 * 服务器处理新的开关会话：先取消治疗并关闭旧交互，再按 bOpen 决定是否打开。
	 * Target 为空时打开自身背包，为普通容器时打开搜索界面，为散落物品时直接尝试拾取。
	 * NewSession 必须大于当前 Session；打开时还需满足行动和目标可达条件。
	 */
	void Open(uint32 NewSession, AExtractionLootActor* Target, bool bOpen);
	/**
	 * 服务器执行物品请求，校验请求序号、背包版本及涉及战利品时的会话、距离和揭示状态。
	 * 支持转移、丢弃、医疗使用及武器配置；武器同步失败时撤回物品变更。
	 * 返回空字符串表示成功，否则返回可展示的错误信息。
	 */
	FString Execute(const FExtractionItemRequest& Request);
	/** 服务器取消能力动作并清除治疗时间和物品 ID，不消耗医疗物品。 */
	void CancelMedical();
	/** 停止角色的开火和瞄准输入。 */
	void StopCombatInput();
	/** 关闭背包并清理当前容器与搜索进度；保留会话编号和已揭示记录，不取消治疗。 */
	void Close();
	/** 服务器从当前容器重建视图，隐藏未揭示内容，记录版本并请求网络更新。 */
	void RefreshLootView();
	/** 服务器在退出时关闭交互，取消治疗、换弹和切枪，并将背包整体掉落。 */
	void DropOnExit();
	/** 计算角色前方的掉落位置，通过遮挡与向下射线检查调整位置。 */
	FVector DropPosition() const;
	/** 查询当前容器中指定物品是否已被此组件揭示；没有当前容器时返回 false。 */
	bool IsRevealed(const FGuid& Id) const;

private:
	/** 服务器最近受理的请求序号，用于拒绝重复或过期请求；后续校验失败也会占用序号。 */
	uint32 LastSequence = 0;
	/** 服务器保存的个人搜索记录：容器 ID 对应已揭示的物品 ID 集合，关闭界面后保留。 */
	TMap<FGuid, TSet<FGuid>> Revealed;
	/** 服务器当前正在搜索的物品 ID；无效值表示尚未选定搜索目标。 */
	FGuid Searching;
	/** 最近一次生成 LootView 时的容器版本；-1 表示尚未建立当前容器的视图。 */
	int32 SeenRevision = -1;
	/** 本地按距离和朝向选择可达的战利品或门；不能行动或背包打开时清空焦点。 */
	void UpdateFocus();
	/** 服务器按槽位与网格顺序逐件搜索，每件耗时 1 秒；Now 为世界时间秒数。 */
	void AdvanceSearch(double Now);
	/** 由服务器请求流程调用：校验医疗需求与动作状态，关闭背包并启动治疗计时；空串表示成功。 */
	FString StartMedical(const FGuid& Id);
};
