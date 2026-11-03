#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ExtractionItems.generated.h"
// 物品定义与实例分离：DataAsset 提供稳定规则，实例保存数量、格位、弹匣、耐久与装配。
// Id 表示独立实例，Definition 指向物品种类，Origins 的 Lot 表示可追踪的来源批次，三者不能混用。
// 容器算法采用副本校验后提交；失败返回 false 并保留原数据，调用方负责权限与网络版本检查。

// 物品类别决定能否装备、堆叠、治疗或装配；不能用 UI 分类替代这些规则。
UENUM(BlueprintType)
enum class EExtractionItemKind : uint8 { Weapon, Ammo, Medical, Valuable, Armor, Attachment, Key, Gear };

/**
 * 胸挂、背包、安全箱：装备槽 4/5/6，各自提供一块网格（Slot -2/-1/-3）。
 * 背包网格沿用旧的 Slot -1，旧存档里的物品因此仍落在"背包"里。
 */
namespace ExtractionGear
{
	constexpr int32 RigSlot = 4, BagSlot = 5, SafeSlot = 6;
	constexpr int32 BagGrid = -1, RigGrid = -2, SafeGrid = -3;
	// 网格分区对应的装备槽；非网格分区返回 -1。
	constexpr int32 SlotForGrid(int32 Grid)
	{
		return Grid == BagGrid ? BagSlot : Grid == RigGrid ? RigSlot : Grid == SafeGrid ? SafeSlot : -1;
	}
	constexpr int32 GridForSlot(int32 Slot)
	{
		return Slot == BagSlot ? BagGrid : Slot == RigSlot ? RigGrid : Slot == SafeSlot ? SafeGrid : 0;
	}
}

UENUM(BlueprintType)
enum class EExtractionAttachmentSlot : uint8
{
	None, Optic, Grip, Muzzle, Receiver, Barrel, Handguard, Stock, Magazine, Rail, PistolGrip
};

// 属性修改区分加法、乘法和覆盖；Priority 参与武器属性解析的顺序。
UENUM()
enum class EWeaponModifierOperation : uint8 { Add, Multiply, Replace };

USTRUCT(BlueprintType)
struct FWeaponAttributeModifier
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	FName Attribute;
	UPROPERTY(EditAnywhere)
	EWeaponModifierOperation Operation = EWeaponModifierOperation::Add;
	UPROPERTY(EditAnywhere)
	float Value = 0;
	UPROPERTY(EditAnywhere)
	int32 Priority = 0;
};

// 精调参数定义：有效范围、步长及曲线共同决定可接受值，不直接信任客户端滑块数值。
USTRUCT(BlueprintType)
struct FWeaponPartTune
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	FName Id;
	UPROPERTY(EditAnywhere)
	float Minimum = 0;
	UPROPERTY(EditAnywhere)
	float Maximum = 100;
	UPROPERTY(EditAnywhere)
	float Default = 50;
	UPROPERTY(EditAnywhere)
	float Step = 1;
	UPROPERTY(EditAnywhere)
	FName Attribute;
	UPROPERTY(EditAnywhere)
	EWeaponModifierOperation Operation = EWeaponModifierOperation::Add;
	UPROPERTY(EditAnywhere)
	TObjectPtr<class UCurveFloat> Curve;
};

USTRUCT()
struct FWeaponTuneValue
{
	GENERATED_BODY()
	UPROPERTY() FName Key;
	UPROPERTY() float Value = 0;
};

UENUM(BlueprintType)
enum class EExtractionMedicalEffect : uint8 { Heal, Bandage, Splint };

/** Stable definitions are cooked assets; instances contain only match state. */
UCLASS(BlueprintType)
class BROKENEDGE_API UExtractionItemDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName ItemId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText Label;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EExtractionItemKind Kind = EExtractionItemKind::Valuable;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FIntPoint Size = FIntPoint(1, 1);
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 MaxStack = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float UseSeconds = 3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float HealAmount = 50;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<class AWeapon> WeaponClass;
	/** Defaults preserve existing AK assets and saved ammunition identities. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon", meta=(ClampMin="1"))
	int32 MagazineCapacity = 30;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	FName DefaultAmmo = TEXT("Ammo");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	TArray<FName> CompatibleAmmo = {TEXT("Ammo"), TEXT("AmmoAP")};
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	TArray<FName> CompatibleAttachments = {TEXT("RedDot"), TEXT("VerticalGrip"), TEXT("Compensator")};
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation")
	TSoftObjectPtr<class UTexture2D> Icon;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation")
	FText Description;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation")
	FName DisplayCategory;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation", meta=(ClampMin="0", ClampMax="5"))
	int32 DisplayRarity = 0;
	// 必须同时属于武器兼容列表且目标定义确实是弹药，防止仅凭名称通过检查。
	bool SupportsAmmo(FName Id) const;
	// 检查武器白名单、附件类型及有效槽位；具体父子装配仍由 ValidateItem 校验。
	bool SupportsAttachment(FName Id) const;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UStaticMesh> WorldMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Penetration = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float DamageScale = 1;
	/** 2 = helmet, 3 = vest; weapon slots 0/1 retain their original meaning. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 ArmorSlot = -1;
	/**
	 * 携行装备：GearSlot 为 4 胸挂 / 5 背包 / 6 安全箱；GearCells 是官方描述里的格数，
	 * 按 GearWidth 列排成网格，最后一行可以不满（官方分仓布局未解出，按总格数还原）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gear")
	int32 GearSlot = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gear", meta=(ClampMin="0", ClampMax="64"))
	int32 GearCells = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gear", meta=(ClampMin="1", ClampMax="8"))
	int32 GearWidth = 6;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 ArmorLevel = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float MaxDurability = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EExtractionMedicalEffect MedicalEffect = EExtractionMedicalEffect::Heal;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EExtractionAttachmentSlot AttachmentSlot = EExtractionAttachmentSlot::None;
	/** 钥匙房房号；与 AExtractionKeyRoom::RequiredKey 对应，为空表示这张卡不开任何门。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Key")
	FName KeyRoomId;
	/** 一张钥匙卡能开门的总次数；实例用 Durability 记录剩余次数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Key", meta=(ClampMin="1", ClampMax="20"))
	int32 KeyUses = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ScatterScale = 1;
	/** Explicit authored compatibility only; unknown source operations must never be enabled. */
	UPROPERTY(EditAnywhere, Category="Gunsmith")
	TArray<FName> ChildAttachments;
	UPROPERTY(EditAnywhere, Category="Gunsmith") TMap<FName, EExtractionAttachmentSlot> MountSockets;
	UPROPERTY(EditAnywhere, Category="Gunsmith")
	TArray<FName> ConflictingAttachments;
	UPROPERTY(EditAnywhere, Category="Gunsmith")
	bool RequiresParent = false;
	UPROPERTY(EditAnywhere, Category="Gunsmith")
	TArray<FWeaponAttributeModifier> WeaponModifiers;
	UPROPERTY(EditAnywhere, Category="Gunsmith")
	TArray<FWeaponPartTune> Tunes;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UStaticMesh> FirstPersonMesh;
	/** Optional rail adapter; its dimensions are authored in the AK component frame. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UStaticMesh> AdapterMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform AdapterMount = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform Mount = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform FirstPersonMount = FTransform::Identity;
	/** Placement override keyed by host weapon item id; Mount stays the legacy fallback. */
	UPROPERTY(EditAnywhere, Category="Gunsmith")
	TMap<FName, FTransform> MountByWeapon;
	UPROPERTY(EditAnywhere, Category="Gunsmith")
	TMap<FName, FTransform> FirstPersonMountByWeapon;
	/** Host-aware placement: weapon specific values win, Mount covers weapons not listed. */
	FTransform Placement(FName WeaponDefinition, bool bFirstPerson) const;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform SupportGrip = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector OpticAimOffset = FVector::ZeroVector;
	/** 按宿主武器覆盖握把支撑手与瞄具瞄准偏移；未列出的武器沿用 SupportGrip / OpticAimOffset。 */
	UPROPERTY(EditAnywhere, Category="Gunsmith")
	TMap<FName, FTransform> SupportGripByWeapon;
	UPROPERTY(EditAnywhere, Category="Gunsmith")
	TMap<FName, FVector> OpticAimOffsetByWeapon;
	/** 自带导轨的宿主武器不需要 AK 坐标系下的转接座。 */
	UPROPERTY(EditAnywhere, Category="Gunsmith")
	TArray<FName> AdapterFreeWeapons;
	UStaticMesh* AdapterFor(FName WeaponDefinition) const;
	FTransform GripFor(FName WeaponDefinition) const;
	FVector AimOffsetFor(FName WeaponDefinition) const;
	// 按固定目录加载白名单定义；未知 ID 返回空指针，调用方需处理缺失资源。
	static const UExtractionItemDefinition* Get(FName Id);
	// 返回可识别的定义白名单，未知名称不会被当作任意资源路径加载。
	static const TArray<FName>& Ids();
};

// 来源数量条目仅用于服务器守恒与恢复；Lot 可跨堆叠、弹匣及不同持有者继续追踪。
USTRUCT()
struct BROKENEDGE_API FExtractionOrigin
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Lot;
	UPROPERTY()
	int32 Quantity = 0;
};

/** Mounted parts remain individual instances, without recursive inventory containers. */
USTRUCT(BlueprintType)
struct BROKENEDGE_API FExtractionAttachment
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Id;
	UPROPERTY()
	FName Definition;
	/** Invalid parent means weapon root, including migrated legacy saves. */
	// 附件父节点为空 GUID 表示挂在枪械根部；非空时必须指向同枪内有效附件且不能成环。
	UPROPERTY()
	FGuid Parent;
	UPROPERTY() FName Socket;
	UPROPERTY()
	TArray<FWeaponTuneValue> TuneValues;
	UPROPERTY(NotReplicated)
	TArray<FExtractionOrigin> Origins;
};

USTRUCT(BlueprintType)
struct BROKENEDGE_API FExtractionItem
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Id;
	UPROPERTY()
	FName Definition;
	UPROPERTY()
	int32 Quantity = 1;
	UPROPERTY()
	FIntPoint Cell = FIntPoint::ZeroValue;
	UPROPERTY()
	FIntPoint Size = FIntPoint(1, 1);
	UPROPERTY()
	bool Rotated = false;
	/** -1 = grid, 0 = held weapon, 1 = slung weapon, 2 = helmet, 3 = vest. */
	// 槽位约定：-1 为普通网格（玩家身上即背包网格），-2 胸挂网格，-3 安全箱网格；
	// 0/1 为主副武器，2/3 为头盔与护甲，4/5/6 为胸挂、背包、安全箱本体。
	UPROPERTY()
	int32 Slot = -1;
	UPROPERTY()
	int32 Magazine = 0;
	UPROPERTY()
	FName MagazineAmmo = TEXT("Ammo");
	UPROPERTY()
	float Durability = 0;
	UPROPERTY()
	TArray<FExtractionAttachment> Attachments;
	/** Loose attachments retain precision tuning when detached or traded. */
	UPROPERTY()
	TArray<FWeaponTuneValue> TuneValues;
	/** Server provenance survives splits, merges and loading into magazines. */
	UPROPERTY(NotReplicated)
	TArray<FExtractionOrigin> Origins;
	// 弹匣的来源账本与本体来源分开，装填、射击及恢复时分别维护。
	UPROPERTY(NotReplicated)
	TArray<FExtractionOrigin> MagazineOrigins;
	// 返回考虑 Rotated 后的占格尺寸；不改变定义原始 Size。
	FIntPoint Extent() const { return Rotated ? FIntPoint(Size.Y, Size.X) : Size; }
};

USTRUCT(BlueprintType)
struct BROKENEDGE_API FExtractionContainer
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Id;
	// 容器内容版本；网络操作携带看到的版本，服务器拒绝过期请求避免覆盖他人修改。
	UPROPERTY()
	int32 Revision = 1;
	UPROPERTY()
	int32 Width = 6;
	UPROPERTY()
	int32 Height = 4;
	UPROPERTY()
	bool WeaponSlots = false;
	/** 玩家携行容器：网格由装备的胸挂/背包/安全箱决定，Width/Height 不再使用；其余容器为固定网格。 */
	UPROPERTY()
	bool GearGrids = false;
	UPROPERTY()
	TArray<FExtractionItem> Items;
	// 按实例 GUID 查找当前容器条目；返回内部数组元素指针，增删数组后应重新查找。
	FExtractionItem* Find(const FGuid& Item);
	const FExtractionItem* Find(const FGuid& Item) const;
	// 按装备槽返回物品，只读查询不改变槽位或物品数量。
	const FExtractionItem* InSlot(int32 Slot) const;
	// 累计指定物品定义的数量，供备弹与交易校验使用。
	int32 Count(FName Definition) const;
};

UENUM()
enum class EExtractionItemOp : uint8 { Transfer, Drop, Use, Attach, Detach, Unload, SelectAmmo, Tune };

USTRUCT()
struct BROKENEDGE_API FExtractionItemRequest
{
	GENERATED_BODY()
	// 请求序号用于丢弃重复或乱序操作；Session 标识这次打开的容器会话，两者职责不同。
	UPROPERTY()
	uint32 Sequence = 0;
	UPROPERTY()
	uint32 Session = 0;
	UPROPERTY()
	EExtractionItemOp Op = EExtractionItemOp::Transfer;
	UPROPERTY()
	bool FromLoot = false;
	UPROPERTY()
	bool ToLoot = false;
	UPROPERTY()
	FGuid Item;
	UPROPERTY()
	FGuid MergeInto;
	/** Server resolves both instances; clients never supply resulting equipment state. */
	UPROPERTY()
	FGuid Weapon;
	UPROPERTY()
	EExtractionAttachmentSlot AttachmentSlot = EExtractionAttachmentSlot::None;
	UPROPERTY()
	FName AmmoDefinition;
	UPROPERTY()
	FGuid ParentAttachment;
	UPROPERTY() FName AttachmentSocket;
	UPROPERTY()
	FGuid MountedAttachment;
	UPROPERTY()
	FName TuneId;
	UPROPERTY()
	float TuneValue = 0;
	UPROPERTY()
	int32 Quantity = 1;
	// 客户端看到的背包与战利品版本；这是并发校验条件，不是新版本赋值。
	UPROPERTY()
	int32 InventoryRevision = 0;
	UPROPERTY()
	int32 LootRevision = 0;
	UPROPERTY()
	FIntPoint Cell = FIntPoint::ZeroValue;
	UPROPERTY()
	int32 Slot = -1;
	UPROPERTY()
	bool Rotated = false;
	UPROPERTY()
	bool AutoPlace = true;
};

/** Copy/validate/commit: a failed operation never changes either container. */
namespace ExtractionItems
{
	// 优先使用装配后解析的容量，解析不可用时退回定义容量，未知定义返回零。
	BROKENEDGE_API int32 MagazineCapacity(const FExtractionItem& Item);
	// 读取解析后的武器参数用于 UI，对单位做换算，并明确这些是项目兼容参数。
	BROKENEDGE_API TArray<FString> WeaponDetails(const FExtractionItem& Item);
	struct FEquipmentAction
	{
		FString Label;
		FExtractionItemRequest Request;
		bool OpenGunsmith = false;
	};

	// 根据当前容器枚举穿脱护甲、装卸附件、弹种及精调操作；这里只生成候选请求，执行时仍需验证兼容性和空间。
	BROKENEDGE_API TArray<FEquipmentAction> EquipmentActions(const FExtractionContainer& Container, FGuid Selected, bool Detailed = false);
	// 从来源列表按顺序取出指定数量并扣减原列表；返回取出的批次组成，清除零数量项。
	BROKENEDGE_API TArray<FExtractionOrigin> TakeOrigins(TArray<FExtractionOrigin>& Source, int32 Quantity);
	// 按 Lot 合并来源数量，物品堆叠和装填时要同时合并批次，不能只改 Quantity。
	BROKENEDGE_API void MergeOrigins(TArray<FExtractionOrigin>& Target, const TArray<FExtractionOrigin>& Source);
	// 重新为物品本体、弹匣和附件建立来源批次；用于新生成或入局追踪，不是普通复制。
	BROKENEDGE_API void SeedOrigins(FExtractionItem& Item);
	// 创建新实例并应用定义尺寸、耐久、默认弹匣，同时初始化来源批次。
	BROKENEDGE_API FExtractionItem Make(FName Definition, int32 Quantity = 1);
	// 一块网格分区：Section 即物品 Slot（-1/-2/-3），Cells 个格子按 Width 列排布，最后一行可不满。
	struct FGrid
	{
		int32 Section = -1;
		int32 Width = 0;
		int32 Cells = 0;
		int32 Rows() const { return Width > 0 ? (Cells + Width - 1) / Width : 0; }
		// 格子是否存在：列在宽度内且线性序号小于总格数。
		bool Has(int32 X, int32 Y) const { return X >= 0 && Y >= 0 && X < Width && Y * Width + X < Cells; }
	};
	// 列出容器当前可用的网格分区；玩家携行容器按胸挂、背包、安全箱顺序，未装备的分区不存在。
	BROKENEDGE_API TArray<FGrid> Grids(const FExtractionContainer& Container);
	BROKENEDGE_API bool GridOf(const FExtractionContainer& Container, int32 Section, FGrid& Out);
	// 安全箱本体及其中物品：死亡不掉落，结算时随玩家回到仓库。
	BROKENEDGE_API bool Protected(const FExtractionItem& Item);
	// 装备槽按类别与占用检查，网格则按旋转后的尺寸检测分区边界与同分区矩形重叠。
	BROKENEDGE_API bool Fits(const FExtractionContainer& Container, const FExtractionItem& Item);
	// 在副本中先合并已有堆叠，再尝试两种朝向逐格放置；全部放下才提交，失败不保留部分堆叠。
	// Section 为 0 时按默认顺序（胸挂、背包；不自动放进安全箱），否则只放进指定分区。
	BROKENEDGE_API bool AutoPlace(FExtractionContainer& Container, FExtractionItem Item, int32 Section = 0);
	// 在来源/目标副本中执行移动、拆分或合并；同容器移动也走统一校验，成功后才替换原容器。
	BROKENEDGE_API bool Transfer(FExtractionContainer& Source, FExtractionContainer& Destination,
	                             const FExtractionItemRequest& Request, FString& Error);
	// 检查容器尺寸、条目与附件身份唯一性、物品合法性及占格；逐个积累已占用区域避免自我碰撞。
	BROKENEDGE_API bool Validate(const FExtractionContainer& Container, FString& Error);
	// 依据定义校验实例字段，并遍历附件父链检测重复槽、缺失父节点、循环、冲突及精调范围。
	// Provenance 为 true 时还要求本体、弹匣、附件来源数量匹配，用于服务器持久化边界。
	BROKENEDGE_API bool ValidateItem(const FExtractionItem& Item, bool Provenance, FString& Error);
	// 先复制容器，再处理安装、整棵子树拆卸、精调、卸弹或换弹种；最终验证通过才递增版本并提交。
	BROKENEDGE_API bool Configure(FExtractionContainer& Container, const FExtractionItemRequest& Request,
	                              FString& Error);
	// 组合已安装附件的散布倍率；完整武器属性解析由 WeaponMechanics 提供。
	BROKENEDGE_API float ScatterScale(const FExtractionItem& Weapon);
	// 生成护甲、武器或普通堆叠的简要展示文本，不作为权威数据来源。
	BROKENEDGE_API FString Details(const FExtractionItem& Item);
	// 把挂载附件恢复为散装物品，保留实例 ID、来源和精调，拆装不应凭空生成另一件资产。
	BROKENEDGE_API FExtractionItem Unmount(const FExtractionAttachment& Part);
}
