#pragma once

#include "CoreMinimal.h"
#include "ExtractionItems.h"
#include "ExtractionProgression.generated.h"
// 长期成长与局内进度分离：服务器只在成功撤离时累积个人任务进度，交易和领奖由仓库事务保存。
// 本文件的 Execute 修改调用者提供的候选对象，不自行回滚或写盘；Persistence 必须在副本上调用并统一提交。

UENUM()
enum class EExtractionEconomyAction : uint8 { Buy, Sell, AcceptQuest, DeliverQuest, ClaimQuest, SelectSafe };

USTRUCT()
struct FExtractionOffer
{
	GENERATED_BODY()
	UPROPERTY() FName Definition;
	UPROPERTY() int32 Price = 0;
	UPROPERTY() int32 Level = 1;
};

USTRUCT()
struct FExtractionQuestDefinition
{
	GENERATED_BODY()
	UPROPERTY() FString Label;
	UPROPERTY() FString Description;
	UPROPERTY() int32 Target = 1;
	UPROPERTY() int32 Credits = 0;
	UPROPERTY() int32 Experience = 0;
};

/** All prototype economy tuning lives here; Game.ini may override these defaults. */
UCLASS(Config=Game, DefaultConfig)
class BROKENEDGE_API UExtractionProgressionRules : public UObject
{
	GENERATED_BODY()
public:
	// 初始化三段等级、商品和三步任务链；Game 配置可覆盖默认调参，但仍需满足配置校验。
	UExtractionProgressionRules();
	UPROPERTY(Config) int32 StartingCredits = 2000;
	UPROPERTY(Config) TArray<int32> LevelExperience;
	UPROPERTY(Config) TArray<FExtractionOffer> Offers;
	UPROPERTY(Config) TArray<FExtractionQuestDefinition> Quests;
	UPROPERTY(Config) int32 ElectronicsSellPrice = 100;
	UPROPERTY(Config) int32 ToolsSellPrice = 200;
	// 按定义 ID 查找商品，返回空表示不在当前商品表中。
	const FExtractionOffer* Offer(FName Definition) const;
	// 按累计经验阈值计算等级，经验不在升级时扣除。
	int32 Level(int64 Experience) const;
	// 计算指定数量的回收报价；护甲按耐久折价，整枪包括弹匣和附件，负数表示不允许出售。
	int64 SellPrice(const FExtractionItem& Item, int32 Quantity) const;
	// 检查三等级三任务结构、单价与奖励范围、定义白名单和唯一商品；任务文件不允许作为商品。
	bool IsValidConfiguration() const;
};

USTRUCT()
struct FExtractionQuestState
{
	GENERATED_BODY()
	UPROPERTY() bool Accepted = false;
	UPROPERTY() bool Claimed = false;
	UPROPERTY() int32 Progress = 0;
	UPROPERTY() bool Delivered = false;
	/** Source lots personally extracted after accepting the file quest. */
	// 本人在接任务后成功带出的合格批次；提交时还要求对应文件实际位于仓库。
	UPROPERTY() TArray<FGuid> EligibleFiles;
};

USTRUCT()
struct FExtractionProgression
{
	GENERATED_BODY()
	UPROPERTY() int64 Credits = 0;
	UPROPERTY() int64 Experience = 0;
	UPROPERTY() TArray<FExtractionQuestState> Quests;
	// 新档案或版本迁移时设置初始货币、经验和三个任务槽，不是每次进入大厅都重置。
	void Initialize();
	// 校验余额经验范围和任务前置关系，保证未接任务不含进度、文件资格不重复。
	bool Validate() const;
};

USTRUCT()
struct FExtractionRaidProgress
{
	GENERATED_BODY()
	// 本局个人守卫击杀计数，死亡或退出不会直接写入长期任务。
	UPROPERTY() int32 GuardKills = 0;
	/** Filled by authority from this raid's key-room recipe, never from a client. */
	UPROPERTY() TArray<FGuid> FileLots;
};

USTRUCT()
struct FExtractionEconomyRequest
{
	GENERATED_BODY()
	// 幂等操作 ID：重试同一操作可返回已保存结果，不能重复扣款或领奖。
	UPROPERTY() FGuid Operation;
	UPROPERTY() int32 Revision = 0;
	UPROPERTY() EExtractionEconomyAction Action = EExtractionEconomyAction::Buy;
	UPROPERTY() FName Definition;
	UPROPERTY() FGuid Item;
	UPROPERTY() int32 Quantity = 1;
	UPROPERTY() int32 Quest = INDEX_NONE;
	// 把请求版本、操作类型及参数编码成回执签名；相同操作 ID 携带不同内容必须被拒绝。
	FString Signature() const;
};

USTRUCT()
struct FExtractionEconomyReceipt
{
	GENERATED_BODY()
	UPROPERTY() FGuid Operation;
	UPROPERTY() FString Signature;
};

namespace ExtractionProgression
{
	/** Mutates a candidate only; persistence commits money, items and receipts together. */
	// 处理购买、出售、接任务、提交文件和领奖；返回 false 时调用者丢弃整个候选状态。
	// 购买分堆放入且枪械为空弹匣，出售同时扣来源，任务文件必须匹配本人接取后成功带出的批次。
	bool Execute(FExtractionProgression& Progression, FExtractionContainer& Stash,
	             const FExtractionEconomyRequest& Request, FString& Error);
	// 只由成功撤离结算调用：累计本人守卫击杀，并登记本局钥匙房文件批次，不替其他队员共享任务。
	void Extract(FExtractionProgression& Progression, const FExtractionContainer& Inventory,
	             const FExtractionRaidProgress& RaidProgress);
	// 生成货币、等级和经验摘要；展示文本不参与金额或等级判定。
	FString Summary(const FExtractionProgression& Progression);
}
