#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ExtractionItems.h"
#include "ExtractionProgression.h"
#include "ExtractionPersistence.generated.h"
// 服务器仓库的单写入者：每条日志保存完整候选状态，包含受影响档案、出战记录和带出批次数量。
// 先校验候选副本，再追加带长度与 CRC 的记录并 Flush，成功后才替换内存 State。
// GameInstanceSubsystem 跨关卡存活；它不是可复制 Actor，客户端通过 AccountComponent 获取自己的快照。

// 持久化结果区分主动离开、断线、超时、崩溃恢复和未开局取消，决定返还与损失语义。
UENUM()
enum class EExtractionOutcome : uint8 { None, Extracted, Dead, Left, Disconnected, TimedOut, Recovered, Aborted };

USTRUCT()
struct FExtractionResult
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Raid;
	UPROPERTY()
	EExtractionOutcome Outcome = EExtractionOutcome::None;
	UPROPERTY()
	TArray<FExtractionItem> Items;
	UPROPERTY()
	int32 OverflowCount = 0;
	UPROPERTY()
	FString ProgressMessage;
};

// 可发给所属客户端的档案快照，不包含服务器保存的凭据哈希。
USTRUCT()
struct FExtractionProfileView
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Store;
	UPROPERTY()
	FGuid Profile;
	UPROPERTY()
	int32 Revision = 0;
	UPROPERTY()
	FExtractionContainer Stash;
	UPROPERTY()
	FExtractionContainer Loadout;
	UPROPERTY()
	TArray<FExtractionItem> Overflow;
	UPROPERTY()
	FExtractionResult Result;
	UPROPERTY()
	FExtractionProgression Progression;
};

// 服务器内部档案：View 是公开给本人的部分，CredentialHash 与幂等回执留在服务端。
USTRUCT()
struct FExtractionStoredProfile
{
	GENERATED_BODY()
	UPROPERTY()
	FExtractionProfileView View;
	UPROPERTY()
	FString CredentialHash;
	UPROPERTY()
	FGuid LastOperation;
	UPROPERTY()
	TArray<FExtractionEconomyReceipt> EconomyReceipts;
};

// 保存入局前的携带原件；恢复时以 Original 和已导出批次计算返还，不采用客户端上报库存。
USTRUCT()
struct FExtractionRaidParticipant
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Profile;
	UPROPERTY()
	FExtractionContainer Original;
	UPROPERTY()
	EExtractionOutcome Outcome = EExtractionOutcome::None;
};

USTRUCT()
struct FExtractionRaidRecord
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Id;
	UPROPERTY()
	bool Active = false;
	UPROPERTY()
	bool Closed = false;
	UPROPERTY()
	int64 ClosedSequence = 0;
	UPROPERTY()
	TArray<FExtractionRaidParticipant> Players;
	// 整局所有成功带出的来源批次数量，跨成员汇总，防止物资转交后被重复返还。
	UPROPERTY()
	TArray<FExtractionOrigin> Exported;
};

USTRUCT()
struct FExtractionStoreState
{
	GENERATED_BODY()
	// 4：出战配置改为携行容器（胸挂/背包/安全箱决定网格），旧配置迁移时补背包与基础安全箱。
	UPROPERTY()
	// 6：胸挂/背包改为分仓布局，旧档案里跨口袋的物品重新摆放；补发入门胸挂、头盔、护甲。
	int32 Version = 8;
	// 日志事务序号逐条递增，加载时拒绝跳号；Version 是结构版本，与事务序号不同。
	UPROPERTY()
	int64 Sequence = 0;
	UPROPERTY()
	FGuid Id;
	UPROPERTY()
	TArray<FExtractionStoredProfile> Profiles;
	UPROPERTY()
	TArray<FExtractionRaidRecord> Raids;
};

UENUM()
enum class EExtractionStorageSide : uint8 { Stash, Loadout, Overflow };

USTRUCT()
struct FExtractionStorageRequest
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Operation;
	UPROPERTY()
	int32 Revision = 0;
	UPROPERTY()
	EExtractionStorageSide From = EExtractionStorageSide::Stash;
	UPROPERTY()
	EExtractionStorageSide To = EExtractionStorageSide::Loadout;
	UPROPERTY()
	FExtractionItemRequest Move;
};

/** Single authoritative writer. A framed, flushed transaction contains the whole store,
 * including all affected profiles and raid export totals. No client-supplied item state. */
UCLASS(Config=Game)
class BROKENEDGE_API UExtractionPersistence : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Initial thresholds follow the recorded WP-08 capacity run; zero disables that trigger.
	UPROPERTY(Config) int64 JournalRollBytes = 64LL * 1024 * 1024;
	UPROPERTY(Config) int32 JournalRollRecords = 128;
	UPROPERTY(Config) int32 JournalGenerations = 3;
	// 按需加载仓库；训练地图使用独立随机目录，避免测试消耗正式档案。
	bool EnsureLoaded();
	// 已有档案必须校验凭据哈希；空身份请求才创建新档案，并通过输出参数返回新凭据。
	bool OpenProfile(FGuid Requested, const FString& Credential, FGuid& Profile, FString& IssuedCredential);
	// 返回当前内存状态中的档案只读指针；找不到返回空，后续 Commit 替换状态后不能继续持有旧指针。
	const FExtractionProfileView* View(FGuid Profile) const;
	// 仓库整理只在候选副本上执行；校验档案版本、操作 ID 和来源/目标，溢出物资只能先领取到仓库。
	bool Move(FGuid Profile, const FExtractionStorageRequest& Request);
	// 先按 Operation 与请求签名识别重试，再检查版本和出战锁；成功交易与回执一起持久化。
	bool Economy(FGuid Profile, const FExtractionEconomyRequest& Request);
	// 为所有成员原子准备出战：保存携带装备原件、重新标记来源批次并清空大厅携行栏。
	// 返回的 Raid ID 是待激活票据，此处尚未开始战斗。
	bool BeginRaid(const TArray<FGuid>& Profiles, FGuid& Raid);
	// 将已准备记录标记为活动并持久化；重复激活同一活动记录视为成功。
	bool ActivateRaid(FGuid Raid);
	// 校验活动记录、成员和背包；已结算成员直接返回成功，避免网络重试重复发放。
	// 只有 Extracted 才入库和积累任务，其他结果记录损失；结果与带出批次在同一事务中提交。
	bool Settle(FGuid Raid, FGuid Profile, EExtractionOutcome Outcome, const FExtractionContainer& Inventory,
	            const FExtractionRaidProgress& Progress = FExtractionRaidProgress());
	// 仅取消尚未激活的出战记录；已经关闭视为成功，活动对局不能走准备取消路径。
	bool AbortRaid(FGuid Raid);
	// 批量取消所有未激活且未关闭记录，供加载失败或开局中止使用；统一提交一次。
	bool AbortPreparedRaids();
	// 按出战 ID 查找记录；返回值指向内部状态，调用方应在当前操作内使用。
	const FExtractionRaidRecord* FindRaid(FGuid Raid) const;
	// 未关闭记录中仍未结算的成员视为正在出战，用于阻止同时整理仓库或重复入局。
	bool IsInRaid(FGuid Profile) const;
	// 仓库实例标识用于区分不同服务器存档，客户端凭据按该 ID 隔离。
	FGuid StoreId() const { return State.Id; }
	// 仅完成加载且没有写入故障时可服务；Loaded 不等于仓库健康。
	bool IsAvailable() const { return Loaded && !Faulted; }
	FString Error;
	// 将持久化结果转换为展示文本；未知或 None 返回空，不改变结算状态。
	static FString OutcomeLabel(EExtractionOutcome Outcome);
#if WITH_DEV_AUTOMATION_TESTS
	friend class FExtractionDriver;
	friend class FExtractionPersistenceCapacity;
	friend class FExtractionJournalRolling;
	friend class FExtractionRetention;
	int32 FailRollStepForTest = 0;
	// 自动化测试专用入口，显式指定隔离目录，避免访问玩家仓库。
	bool LoadForTest(const FString& Directory);
	bool FailNextWrite = false;
	// 提供自动化测试只读快照，不作为客户端查询接口。
	const FExtractionStoreState& TestState() const { return State; }
#endif

private:
	static void PruneHistory(FExtractionStoreState& Candidate);
	int32 JournalRecords = 0;
	bool ReadSegment(const FString& Path, FExtractionStoreState& Out) const;
	bool WriteSegment(const FString& Path) const;
	bool RecoverPendingSegment();
	bool RollJournal();
	UPROPERTY()
	FExtractionStoreState State;
	// 服务器本地追加日志路径；不通过网络复制，不应把凭据内容写入日志。
	FString Journal;
	bool Loaded = false;
	bool TrainingStore = false;
	// 写入失败后的熔断状态；在重新加载前拒绝继续提交，避免内存与磁盘分叉。
	bool Faulted = false;
	// 顺序读取日志，检查帧头、长度、CRC、序号和内容；完整损坏记录直接报错，不静默丢弃。
	// 只有末尾未写完整的记录可截断；旧版先保留原始备份，再迁移并恢复未结算对局。
	bool Load(const FString& Directory);
	// 按值接收候选快照，成功刷盘后才发布为当前 State；写入失败锁定 Faulted，需恢复存储并重启。
	bool Commit(FExtractionStoreState Candidate);
	// 重启时处理未关闭对局：从入局原物资扣除已被任何成员带出的来源批次，只返还剩余部分。
	// 数量拆分后必要时更换实例 ID，避免与已经进入其他仓库的物品身份冲突。
	bool Recover();
	// 在候选状态中返还尚未结算成员的原装备并标记 Aborted；此辅助函数本身不刷盘。
	static void AbortPreparedRecord(FExtractionStoreState& Candidate, FExtractionRaidRecord& Raid);
	// 检查存储版本、档案与出战身份唯一性、容器合法性及物品来源；不接受客户端自行拼出的结果状态。
	bool Validate(const FExtractionStoreState& Candidate) const;
	// 将物品尽量自动放入仓库，放不下则放入 Overflow；这是内存修改，调用方负责最终 Commit。
	static void Deposit(FExtractionProfileView& Profile, TArray<FExtractionItem> Items);
};
