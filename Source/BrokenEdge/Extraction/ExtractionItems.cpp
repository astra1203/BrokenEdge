#include "ExtractionItems.h"
// 物品定义与实例分离：DataAsset 提供稳定规则，实例保存数量、格位、弹匣、耐久与装配。
// Id 表示独立实例，Definition 指向物品种类，Origins 的 Lot 表示可追踪的来源批次，三者不能混用。
// 容器算法采用副本校验后提交；失败返回 false 并保留原数据，调用方负责权限与网络版本检查。

// 必须同时属于武器兼容列表且目标定义确实是弹药，防止仅凭名称通过检查。
bool UExtractionItemDefinition::SupportsAmmo(FName Id) const
{
	const auto* Ammo = Get(Id);
	return Kind == EExtractionItemKind::Weapon && CompatibleAmmo.Contains(Id)
		&& Ammo && Ammo->Kind == EExtractionItemKind::Ammo;
}

// 检查武器白名单、附件类型及有效槽位；具体父子装配仍由 ValidateItem 校验。
bool UExtractionItemDefinition::SupportsAttachment(FName Id) const
{
	const auto* Part = Get(Id);
	return Kind == EExtractionItemKind::Weapon && CompatibleAttachments.Contains(Id)
		&& Part && Part->Kind == EExtractionItemKind::Attachment
		&& Part->AttachmentSlot != EExtractionAttachmentSlot::None;
}

FTransform UExtractionItemDefinition::Placement(FName WeaponDefinition, bool bFirstPerson) const
{
	const auto& Overrides = bFirstPerson ? FirstPersonMountByWeapon : MountByWeapon;
	if (const FTransform* Explicit = Overrides.Find(WeaponDefinition)) return *Explicit;
	return bFirstPerson ? FirstPersonMount : Mount;
}

FTransform UExtractionItemDefinition::GripFor(FName WeaponDefinition) const
{
	const FTransform* Explicit = SupportGripByWeapon.Find(WeaponDefinition);
	return Explicit ? *Explicit : SupportGrip;
}

UStaticMesh* UExtractionItemDefinition::AdapterFor(FName WeaponDefinition) const
{
	return AdapterFreeWeapons.Contains(WeaponDefinition) ? nullptr : AdapterMesh.Get();
}

FVector UExtractionItemDefinition::AimOffsetFor(FName WeaponDefinition) const
{
	const FVector* Explicit = OpticAimOffsetByWeapon.Find(WeaponDefinition);
	return Explicit ? *Explicit : OpticAimOffset;
}

// 返回可识别的定义白名单，未知名称不会被当作任意资源路径加载。
const TArray<FName>& UExtractionItemDefinition::Ids()
{
	static const TArray<FName> Names = {
		TEXT("AK"), TEXT("Ammo"), TEXT("Medkit"), TEXT("Electronics"), TEXT("Tools"),
		TEXT("AmmoAP"), TEXT("Helmet2"), TEXT("Helmet3"), TEXT("Vest2"), TEXT("Vest3"), TEXT("Bandage"), TEXT("Splint"),
		TEXT("RedDot"), TEXT("VerticalGrip"), TEXT("Compensator"), TEXT("Key"), TEXT("Document"),
		TEXT("SCARH"), TEXT("Ammo76251"),
		// 通用武器流水线（Scripts/Extraction/weapon_specs.py）生成的枪械与弹种
		TEXT("M4A1"), TEXT("Ammo55645"),
		// 项目自定武器（Military Weapons Silver 模型），由 configure_extra_weapons.py 生成
		TEXT("BoltSniper"), TEXT("Ammo_338"), TEXT("PumpShotgun"), TEXT("Ammo_12G"), TEXT("Pistol9"), TEXT("Ammo_9mm"),
		// 航天基地钥匙卡：房号见 AExtractionKeyRoom::RequiredKey
		TEXT("Key_President"), TEXT("Key_Core"), TEXT("Key_Monitor"), TEXT("Key_Contract"),
		TEXT("Key_EastBridge"), TEXT("Key_StaffPassage"),
		// 携行装备：官方 EquipmentPart / LOC_GameItem 中的胸挂、背包、安全箱
		TEXT("Rig_Pouch"), TEXT("Rig_Ammo"), TEXT("Rig_Assault"), TEXT("Rig_Hurricane"),
		TEXT("Bag_Climb"), TEXT("Bag_Hike"), TEXT("Bag_ALS"), TEXT("Bag_GTO"),
		TEXT("Safe_Basic"), TEXT("Safe_Advanced"), TEXT("Safe_Premium"), TEXT("Safe_Field"),
		// 投掷物：官方增强型破片手雷 21010000001
		TEXT("FragGrenade"),
		// 航天基地官方收集品（GameItem.SourceMapId=3902），由 generate_collectibles.py 生成
#include "ExtractionCollectibles.inl"
	};
	return Names;
}

// 按固定目录加载白名单定义；未知 ID 返回空指针，调用方需处理缺失资源。
const UExtractionItemDefinition* UExtractionItemDefinition::Get(FName Id)
{
	static const TSet<FName> Known(Ids());
	if (!Known.Contains(Id)) return nullptr;
	return LoadObject<UExtractionItemDefinition>(nullptr,
	                                             *FString::Printf(
		                                             TEXT("/Game/Extraction/Items/DA_Item_%s.DA_Item_%s"),
		                                             *Id.ToString(), *Id.ToString()));
}

// 按实例 GUID 查找当前容器条目；返回内部数组元素指针，增删数组后应重新查找。
FExtractionItem* FExtractionContainer::Find(const FGuid& Item)
{
	return Items.FindByPredicate([&](const FExtractionItem& I) { return I.Id == Item; });
}

// 按实例 GUID 查找当前容器条目；返回内部数组元素指针，增删数组后应重新查找。
const FExtractionItem* FExtractionContainer::Find(const FGuid& Item) const
{
	return Items.FindByPredicate([&](const FExtractionItem& I) { return I.Id == Item; });
}

// 按装备槽返回物品，只读查询不改变槽位或物品数量。
const FExtractionItem* FExtractionContainer::InSlot(int32 Slot) const
{
	return Items.FindByPredicate([&](const FExtractionItem& I) { return I.Slot == Slot; });
}

// 累计指定物品定义的数量，供备弹与交易校验使用。
int32 FExtractionContainer::Count(FName Definition) const
{
	int32 Result = 0;
	for (const auto& I : Items) if (I.Definition == Definition) Result += I.Quantity;
	return Result;
}

// 创建新实例并应用定义尺寸、耐久、默认弹匣，同时初始化来源批次。
FExtractionItem ExtractionItems::Make(FName Definition, int32 Quantity)
{
	FExtractionItem Item;
	Item.Id = FGuid::NewGuid();
	Item.Definition = Definition;
	Item.Quantity = Quantity;
	if (const auto* D = UExtractionItemDefinition::Get(Definition))
	{
		Item.Size = D->Size;
		// 钥匙卡用 Durability 记录剩余开门次数；护甲沿用原耐久值。
		Item.Durability = D->Kind == EExtractionItemKind::Key ? float(D->KeyUses) : D->MaxDurability;
		if (D->Kind == EExtractionItemKind::Weapon)
		{
			Item.Magazine = D->MagazineCapacity;
			Item.MagazineAmmo = D->DefaultAmmo;
		}
	}
	SeedOrigins(Item);
	return Item;
}

// 重新为物品本体、弹匣和附件建立来源批次；用于新生成或入局追踪，不是普通复制。
void ExtractionItems::SeedOrigins(FExtractionItem& Item)
{
	Item.Origins = {{FGuid::NewGuid(), Item.Quantity}};
	Item.MagazineOrigins.Reset();
	if (Item.Magazine) Item.MagazineOrigins.Add({FGuid::NewGuid(), Item.Magazine});
	for (auto& Part : Item.Attachments) Part.Origins = {{FGuid::NewGuid(), 1}};
}

// 从来源列表按顺序取出指定数量并扣减原列表；返回取出的批次组成，清除零数量项。
TArray<FExtractionOrigin> ExtractionItems::TakeOrigins(TArray<FExtractionOrigin>& Source, int32 Quantity)
{
	TArray<FExtractionOrigin> Result;
	for (auto& Part : Source)
	{
		const int32 Taken = FMath::Min(Part.Quantity, Quantity);
		if (Taken > 0) Result.Add({Part.Lot, Taken});
		Part.Quantity -= Taken;
		Quantity -= Taken;
	}
	Source.RemoveAll([](const FExtractionOrigin& P) { return P.Quantity == 0; });
	return Result;
}

// 按 Lot 合并来源数量，物品堆叠和装填时要同时合并批次，不能只改 Quantity。
void ExtractionItems::MergeOrigins(TArray<FExtractionOrigin>& Target, const TArray<FExtractionOrigin>& Source)
{
	for (const auto& Part : Source)
	{
		auto* Existing = Target.FindByPredicate([&](const FExtractionOrigin& P) { return P.Lot == Part.Lot; });
		if (Existing) Existing->Quantity += Part.Quantity;
		else Target.Add(Part);
	}
}

bool ExtractionItems::GridOf(const FExtractionContainer& Container, int32 Section, FGrid& Out)
{
	Out = FGrid();
	Out.Section = Section;
	if (!Container.GearGrids)
	{
		if (Section != -1) return false;
		Out.Width = Container.Width;
		Out.Cells = Container.Width * Container.Height;
		return Out.Cells > 0;
	}
	const auto* Gear = Container.InSlot(ExtractionGear::SlotForGrid(Section));
	const auto* D = Gear ? UExtractionItemDefinition::Get(Gear->Definition) : nullptr;
	if (!D || D->Kind != EExtractionItemKind::Gear || D->GearCells <= 0) return false;
	Out.Width = FMath::Clamp(D->GearWidth, 1, 8);
	Out.Cells = D->GearCells;
	if (const TArray<FIntRect>* Layout = PocketLayout(Gear->Definition))
	{
		Out.Pockets.Append(*Layout);
		Out.Width = 0;
		Out.Cells = 0;
		for (const FIntRect& P : *Layout)
		{
			Out.Width = FMath::Max(Out.Width, P.Max.X);
			Out.Cells += P.Area();
		}
	}
	return true;
}

// 每种胸挂、背包的口袋排法（项目自定，总格数与描述一致）：胸挂以竖排弹匣袋为主，
// 背包是一个主仓加若干侧袋/顶袋。坐标为 (列, 行, 宽, 高)。
namespace { bool GLegacyGearGrids = false; }

ExtractionItems::FLegacyGridScope::FLegacyGridScope(bool bLegacy) : bPrevious(GLegacyGearGrids)
{
	GLegacyGearGrids = bLegacy;
}

ExtractionItems::FLegacyGridScope::~FLegacyGridScope() { GLegacyGearGrids = bPrevious; }

const TArray<FIntRect>* ExtractionItems::PocketLayout(FName Definition)
{
	if (GLegacyGearGrids) return nullptr;
	auto R = [](int32 X, int32 Y, int32 W, int32 H) { return FIntRect(X, Y, X + W, Y + H); };
	static const TMap<FName, TArray<FIntRect>> Layouts = {
		// 便携胸包 6：一个 2×2 主袋 + 一个 1×2 侧袋。
		{TEXT("Rig_Pouch"), {R(0, 0, 2, 2), R(2, 0, 1, 2)}},
		// 简易携行弹挂 10：五个 1×2 弹匣袋。
		{TEXT("Rig_Ammo"), {R(0, 0, 1, 2), R(1, 0, 1, 2), R(2, 0, 1, 2), R(3, 0, 1, 2), R(4, 0, 1, 2)}},
		// 突击者战术背心 16：四个 1×2 弹匣袋 + 两个 2×2 杂物袋。
		{TEXT("Rig_Assault"), {R(0, 0, 1, 2), R(1, 0, 1, 2), R(2, 0, 1, 2), R(3, 0, 1, 2), R(4, 0, 2, 2), R(6, 0, 2, 2)}},
		// 飓风战术胸挂 22：上排六个 1×2 弹匣袋，下排两个 2×2 和一个 2×1。
		{TEXT("Rig_Hurricane"), {R(0, 0, 1, 2), R(1, 0, 1, 2), R(2, 0, 1, 2), R(3, 0, 1, 2), R(4, 0, 1, 2), R(5, 0, 1, 2),
		                         R(0, 2, 2, 2), R(2, 2, 2, 2), R(4, 2, 2, 1)}},
		// 大型登山包 16：4×3 主仓 + 2×2 顶袋。
		{TEXT("Bag_Climb"), {R(0, 0, 4, 3), R(0, 3, 2, 2)}},
		// 野战徒步背包 24：6×3 主仓 + 两个 3×1 前袋。
		{TEXT("Bag_Hike"), {R(0, 0, 6, 3), R(0, 3, 3, 1), R(3, 3, 3, 1)}},
		// ALS 背负系统 28：5×4 主仓 + 1×4 侧袋 + 2×2 顶袋。
		{TEXT("Bag_ALS"), {R(0, 0, 5, 4), R(5, 0, 1, 4), R(0, 4, 2, 2)}},
		// GTO 重型战术包 45：6×5 主仓 + 2×3、2×2 两个侧袋 + 5×1 底袋。
		{TEXT("Bag_GTO"), {R(0, 0, 6, 5), R(6, 0, 2, 3), R(6, 3, 2, 2), R(0, 5, 5, 1)}},
	};
	return Layouts.Find(Definition);
}

TArray<ExtractionItems::FGrid> ExtractionItems::Grids(const FExtractionContainer& Container)
{
	TArray<FGrid> Result;
	for (int32 Section : {ExtractionGear::RigGrid, ExtractionGear::BagGrid, ExtractionGear::SafeGrid})
		if (FGrid Grid; GridOf(Container, Section, Grid)) Result.Add(Grid);
	return Result;
}

bool ExtractionItems::Protected(const FExtractionItem& Item)
{
	return Item.Slot == ExtractionGear::SafeSlot || Item.Slot == ExtractionGear::SafeGrid;
}

bool ExtractionItems::IsSafeBox(const FExtractionItem& Item)
{
	const auto* D = UExtractionItemDefinition::Get(Item.Definition);
	return D && D->Kind == EExtractionItemKind::Gear && D->GearSlot == ExtractionGear::SafeSlot;
}

bool ExtractionItems::DetachGear(FExtractionContainer& Inventory, const FGuid& GearId, FExtractionContainer& Out, FString& Error)
{
	const auto* Gear = Inventory.Find(GearId);
	if (!Gear || !Inventory.GearGrids || (Gear->Slot != ExtractionGear::RigSlot && Gear->Slot != ExtractionGear::BagSlot))
	{
		Error = TEXT("只能整包丢弃身上的胸挂或背包");
		return false;
	}
	const int32 Grid = ExtractionGear::GridForSlot(Gear->Slot);
	FExtractionContainer From = Inventory, To = Out;
	To.GearGrids = true;
	To.WeaponSlots = true;
	To.Items.Reset();
	for (const auto& I : From.Items)
		if (I.Id == GearId || I.Slot == Grid) To.Items.Add(I);
	From.Items.RemoveAll([&](const FExtractionItem& I) { return I.Id == GearId || I.Slot == Grid; });
	if (!Validate(From, Error) || !Validate(To, Error)) return false;
	++From.Revision;
	++To.Revision;
	Inventory = MoveTemp(From);
	Out = MoveTemp(To);
	return true;
}

bool ExtractionItems::AttachGear(FExtractionContainer& Inventory, FExtractionContainer& Bundle, FString& Error)
{
	const FExtractionItem* Gear = nullptr;
	for (const auto& I : Bundle.Items)
		if (I.Slot == ExtractionGear::RigSlot || I.Slot == ExtractionGear::BagSlot) Gear = &I;
	if (!Gear || !Bundle.GearGrids || !Inventory.GearGrids || Inventory.InSlot(Gear->Slot))
	{
		Error = TEXT("对应装备槽已占用");
		return false;
	}
	const int32 Grid = ExtractionGear::GridForSlot(Gear->Slot);
	FExtractionContainer To = Inventory, From = Bundle;
	TArray<FGuid> Moved;
	for (const auto& I : From.Items)
		if (I.Id == Gear->Id || I.Slot == Grid)
		{
			To.Items.Add(I);
			Moved.Add(I.Id);
		}
	From.Items.RemoveAll([&](const FExtractionItem& I) { return Moved.Contains(I.Id); });
	if (!Validate(To, Error) || !Validate(From, Error)) return false;
	++From.Revision;
	++To.Revision;
	Inventory = MoveTemp(To);
	Bundle = MoveTemp(From);
	return true;
}

// 装备槽按类别与占用检查，网格则按旋转后的尺寸检测分区边界与同分区矩形重叠。
bool ExtractionItems::Fits(const FExtractionContainer& Container, const FExtractionItem& Item)
{
	if (Item.Slot >= 0)
	{
		const auto* D = UExtractionItemDefinition::Get(Item.Definition);
		const bool Gear = Item.Slot >= ExtractionGear::RigSlot && Item.Slot <= ExtractionGear::SafeSlot;
		return Container.WeaponSlots && D && ((Item.Slot < 2 && D->Kind == EExtractionItemKind::Weapon)
				|| (Item.Slot >= 2 && Item.Slot <= 3 && D->Kind == EExtractionItemKind::Armor && D->ArmorSlot == Item.
					Slot)
				|| (Gear && Container.GearGrids && D->Kind == EExtractionItemKind::Gear && D->GearSlot == Item.Slot))
			&& !Container.InSlot(Item.Slot);
	}
	FGrid Grid;
	if (!GridOf(Container, Item.Slot, Grid)) return false;
	const FIntPoint Size = Item.Extent();
	if (!Grid.Holds(Item.Cell, Size)) return false;
	for (const auto& Other : Container.Items)
	{
		if (Other.Slot != Item.Slot) continue;
		const FIntPoint OtherSize = Other.Extent();
		if (Item.Cell.X < Other.Cell.X + OtherSize.X && Item.Cell.X + Size.X > Other.Cell.X
			&& Item.Cell.Y < Other.Cell.Y + OtherSize.Y && Item.Cell.Y + Size.Y > Other.Cell.Y)
			return false;
	}
	return true;
}

// 在副本中先合并已有堆叠，再尝试两种朝向逐格放置；全部放下才提交，失败不保留部分堆叠。
bool ExtractionItems::AutoPlace(FExtractionContainer& Container, FExtractionItem Item, int32 Section)
{
	const auto* D = UExtractionItemDefinition::Get(Item.Definition);
	if (!D || Item.Quantity <= 0 || Item.Quantity > D->MaxStack) return false;
	FExtractionContainer Candidate = Container;
	// 默认顺序先胸挂再背包；有胸挂或背包时安全箱只接受明确指定，避免捡东西时被自动塞进去。
	// 身上只剩安全箱（没胸挂也没背包）时，Shift/双击转移和拾取就放进安全箱。
	TArray<int32> Sections;
	if (Section < 0) Sections.Add(Section);
	else if (Container.GearGrids)
	{
		FGrid Probe;
		if (GridOf(Container, ExtractionGear::RigGrid, Probe) || GridOf(Container, ExtractionGear::BagGrid, Probe))
			Sections = {ExtractionGear::RigGrid, ExtractionGear::BagGrid};
		else Sections = {ExtractionGear::SafeGrid};
	}
	else Sections.Add(-1);
	if (D->MaxStack > 1)
		for (auto& Other : Candidate.Items)
		{
			if (Other.Definition != Item.Definition || Other.Quantity >= D->MaxStack || !Sections.Contains(Other.Slot))
				continue;
			const int32 Count = FMath::Min(D->MaxStack - Other.Quantity, Item.Quantity);
			MergeOrigins(Other.Origins, TakeOrigins(Item.Origins, Count));
			Other.Quantity += Count;
			Item.Quantity -= Count;
			if (!Item.Quantity)
			{
				Container = MoveTemp(Candidate);
				return true;
			}
		}
	for (int32 Target : Sections)
	{
		FGrid Grid;
		if (!GridOf(Candidate, Target, Grid)) continue;
		Item.Slot = Target;
		for (int32 Rotation = 0; Rotation < 2; ++Rotation)
		{
			Item.Rotated = Rotation != 0;
			for (int32 Y = 0; Y < Grid.Rows(); ++Y)
				for (int32 X = 0; X < Grid.Width; ++X)
				{
					Item.Cell = FIntPoint(X, Y);
					if (Fits(Candidate, Item))
					{
						Candidate.Items.Add(Item);
						Container = MoveTemp(Candidate);
						return true;
					}
				}
		}
	}
	return false;
}

bool ExtractionItems::Sort(FExtractionContainer& Container)
{
	// 种类顺序：武器、弹药、医疗、投掷物、配件、钥匙、护甲、携行装备、其余收集品。
	auto KindOrder = [](const UExtractionItemDefinition* D)
	{
		if (!D) return 99;
		switch (D->Kind)
		{
		case EExtractionItemKind::Weapon: return 0;
		case EExtractionItemKind::Ammo: return 1;
		case EExtractionItemKind::Medical: return 2;
		case EExtractionItemKind::Throwable: return 3;
		case EExtractionItemKind::Attachment: return 4;
		case EExtractionItemKind::Key: return 5;
		case EExtractionItemKind::Armor: return 6;
		case EExtractionItemKind::Gear: return 7;
		default: return 8;
		}
	};
	auto Area = [](const FExtractionItem& Item) { return Item.Size.X * Item.Size.Y; };
	// 先按种类分组排；装不下时退回纯按占格从大到小，更容易塞满。
	for (int32 Attempt = 0; Attempt < 2; ++Attempt)
	{
		FExtractionContainer Result = Container;
		bool bPlaced = true;
		for (const FGrid& Grid : Grids(Container))
		{
			TArray<FExtractionItem> Section;
			for (int32 Index = Result.Items.Num() - 1; Index >= 0; --Index)
				if (Result.Items[Index].Slot == Grid.Section) Section.Add(Result.Items[Index]), Result.Items.RemoveAt(Index);
			Section.Sort([&](const FExtractionItem& A, const FExtractionItem& B)
			{
				const auto* DA = UExtractionItemDefinition::Get(A.Definition);
				const auto* DB = UExtractionItemDefinition::Get(B.Definition);
				if (Attempt == 0 && KindOrder(DA) != KindOrder(DB)) return KindOrder(DA) < KindOrder(DB);
				if (Area(A) != Area(B)) return Area(A) > Area(B);
				const int32 RA = DA ? DA->DisplayRarity : 0, RB = DB ? DB->DisplayRarity : 0;
				if (RA != RB) return RA > RB;
				if (A.Definition != B.Definition) return A.Definition.LexicalLess(B.Definition);
				return A.Quantity > B.Quantity;
			});
			for (FExtractionItem& Item : Section)
			{
				Item.Rotated = false;
				if (!AutoPlace(Result, Item, Grid.Section))
				{
					bPlaced = false;
					break;
				}
			}
			if (!bPlaced) break;
		}
		if (bPlaced)
		{
			++Result.Revision;
			Container = MoveTemp(Result);
			return true;
		}
	}
	return false;
}

// 检查容器尺寸、条目与附件身份唯一性、物品合法性及占格；逐个积累已占用区域避免自我碰撞。
bool ExtractionItems::Validate(const FExtractionContainer& Container, FString& Error)
{
	if (Container.Width < 1 || Container.Width > 16 || Container.Height < 1 || Container.Height > 16)
	{
		Error = TEXT("容器尺寸无效");
		return false;
	}
	FExtractionContainer Occupied = Container;
	Occupied.Items.Reset();
	TSet<FGuid> Ids;
	// 先放装备槽再放网格：网格分区由胸挂/背包/安全箱决定，顺序颠倒会把合法物品判成越界。
	TArray<const FExtractionItem*> Ordered;
	for (const auto& I : Container.Items) if (I.Slot >= 0) Ordered.Add(&I);
	for (const auto& I : Container.Items) if (I.Slot < 0) Ordered.Add(&I);
	for (const auto* Entry : Ordered)
	{
		const auto& I = *Entry;
		if (Ids.Contains(I.Id) || !ValidateItem(I, false, Error) || !Fits(Occupied, I))
		{
			Error = TEXT("物品或占格无效");
			return false;
		}
		Ids.Add(I.Id);
		for (const auto& Part : I.Attachments)
		{
			if (Ids.Contains(Part.Id))
			{
				Error = TEXT("附件身份冲突");
				return false;
			}
			Ids.Add(Part.Id);
		}
		Occupied.Items.Add(I);
	}
	return true;
}

// 在来源/目标副本中执行移动、拆分或合并；同容器移动也走统一校验，成功后才替换原容器。
bool ExtractionItems::Transfer(FExtractionContainer& Source, FExtractionContainer& Destination,
                               const FExtractionItemRequest& Request, FString& Error)
{
	const auto* Original = Source.Find(Request.Item);
	if (!Original || Request.Quantity <= 0 || Request.Quantity > Original->Quantity)
	{
		Error = TEXT("物品已变化或数量无效");
		return false;
	}
	const auto* D = UExtractionItemDefinition::Get(Original->Definition);
	if (!D || Request.Slot < ExtractionGear::SafeGrid || Request.Slot > ExtractionGear::SafeSlot)
	{
		Error = TEXT("目标位置无效");
		return false;
	}
	// 胸挂/背包/安全箱里还有东西时不能取下，否则里面的物品会失去所在网格。
	if (Source.GearGrids && Original->Slot >= ExtractionGear::RigSlot && Original->Slot <= ExtractionGear::SafeSlot
		&& Source.Items.ContainsByPredicate([&](const FExtractionItem& I)
		{
			return I.Slot == ExtractionGear::GridForSlot(Original->Slot);
		}))
	{
		Error = TEXT("请先清空这件装备里的物品");
		return false;
	}
	const bool Same = &Source == &Destination;
	FExtractionContainer From = Source, To = Destination;
	FExtractionContainer& Target = Same ? From : To;
	FExtractionItem Moving = *Original;
	Moving.Quantity = Request.Quantity;
	if (Moving.Quantity == Original->Quantity)
		From.Items.RemoveAll([&](const FExtractionItem& I)
		{
			return I.Id == Request.Item;
		});
	else
	{
		From.Find(Request.Item)->Quantity -= Moving.Quantity;
		Moving.Origins = TakeOrigins(From.Find(Request.Item)->Origins, Moving.Quantity);
		Moving.Id = FGuid::NewGuid();
	}
	Moving.Cell = Request.Cell;
	Moving.Slot = Request.Slot;
	Moving.Rotated = Request.Rotated;
	bool Placed = false;
	if (Request.MergeInto.IsValid())
	{
		auto* Merge = Target.Find(Request.MergeInto);
		if (Merge && Merge->Definition == Moving.Definition && D->MaxStack > 1
			&& Merge->Quantity <= D->MaxStack - Moving.Quantity)
		{
			Merge->Quantity += Moving.Quantity;
			MergeOrigins(Merge->Origins, Moving.Origins);
			Placed = true;
		}
	}
	// Slot -1 自动放置走默认顺序（胸挂→背包）；-2/-3 只放进指定分区。
	else if (Request.AutoPlace && Request.Slot < 0)
		Placed = AutoPlace(Target, Moving, Request.Slot == -1 ? 0 : Request.Slot);
	else if (Fits(Target, Moving))
	{
		Target.Items.Add(Moving);
		Placed = true;
	}
	if (!Placed)
	{
		Error = TEXT("空间不足、位置占用或无法堆叠");
		return false;
	}
	if (!Validate(From, Error) || (!Same && !Validate(To, Error))) return false;
	if (!Same)
	{
		TSet<FGuid> DestinationIds;
		for (const auto& I : To.Items)
		{
			DestinationIds.Add(I.Id);
			for (const auto& Part : I.Attachments) DestinationIds.Add(Part.Id);
		}
		for (const auto& I : From.Items)
		{
			bool Conflict = DestinationIds.Contains(I.Id);
			for (const auto& Part : I.Attachments) Conflict |= DestinationIds.Contains(Part.Id);
			if (Conflict)
			{
				Error = TEXT("物品身份冲突");
				return false;
			}
		}
	}
	++From.Revision;
	Source = MoveTemp(From);
	if (!Same)
	{
		++To.Revision;
		Destination = MoveTemp(To);
	}
	return true;
}
