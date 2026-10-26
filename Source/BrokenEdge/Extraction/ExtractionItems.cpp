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

// 返回可识别的定义白名单，未知名称不会被当作任意资源路径加载。
const TArray<FName>& UExtractionItemDefinition::Ids()
{
	static const TArray<FName> Names = {
		TEXT("AK"), TEXT("Ammo"), TEXT("Medkit"), TEXT("Electronics"), TEXT("Tools"),
		TEXT("AmmoAP"), TEXT("Helmet2"), TEXT("Helmet3"), TEXT("Vest2"), TEXT("Vest3"), TEXT("Bandage"), TEXT("Splint"),
		TEXT("RedDot"), TEXT("VerticalGrip"), TEXT("Compensator"), TEXT("Key"), TEXT("Document"),
		TEXT("SCARH"), TEXT("Ammo76251"),
		// 航天基地钥匙卡：房号见 AExtractionKeyRoom::RequiredKey
		TEXT("Key_President"), TEXT("Key_Core"), TEXT("Key_Monitor"), TEXT("Key_Contract"),
		TEXT("Key_EastBridge"), TEXT("Key_StaffPassage")
	};
	return Names;
}

// 按固定目录加载白名单定义；未知 ID 返回空指针，调用方需处理缺失资源。
const UExtractionItemDefinition* UExtractionItemDefinition::Get(FName Id)
{
	if (!Ids().Contains(Id)) return nullptr;
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

// 装备槽按类别与占用检查，网格则按旋转后的尺寸检测边界与矩形重叠。
bool ExtractionItems::Fits(const FExtractionContainer& Container, const FExtractionItem& Item)
{
	if (Item.Slot >= 0)
	{
		const auto* D = UExtractionItemDefinition::Get(Item.Definition);
		return Container.WeaponSlots && D && ((Item.Slot < 2 && D->Kind == EExtractionItemKind::Weapon)
				|| (Item.Slot >= 2 && Item.Slot <= 3 && D->Kind == EExtractionItemKind::Armor && D->ArmorSlot == Item.
					Slot))
			&& !Container.InSlot(Item.Slot);
	}
	if (Item.Slot != -1) return false;
	const FIntPoint Size = Item.Extent();
	if (Size.X <= 0 || Size.Y <= 0 || Item.Cell.X < 0 || Item.Cell.Y < 0
		|| Item.Cell.X > Container.Width - Size.X || Item.Cell.Y > Container.Height - Size.Y)
		return false;
	for (const auto& Other : Container.Items)
	{
		if (Other.Slot >= 0) continue;
		const FIntPoint OtherSize = Other.Extent();
		if (Item.Cell.X < Other.Cell.X + OtherSize.X && Item.Cell.X + Size.X > Other.Cell.X
			&& Item.Cell.Y < Other.Cell.Y + OtherSize.Y && Item.Cell.Y + Size.Y > Other.Cell.Y)
			return false;
	}
	return true;
}

// 在副本中先合并已有堆叠，再尝试两种朝向逐格放置；全部放下才提交，失败不保留部分堆叠。
bool ExtractionItems::AutoPlace(FExtractionContainer& Container, FExtractionItem Item)
{
	const auto* D = UExtractionItemDefinition::Get(Item.Definition);
	if (!D || Item.Quantity <= 0 || Item.Quantity > D->MaxStack) return false;
	FExtractionContainer Candidate = Container;
	if (D->MaxStack > 1)
		for (auto& Other : Candidate.Items)
		{
			if (Other.Definition != Item.Definition || Other.Quantity >= D->MaxStack) continue;
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
	Item.Slot = -1;
	for (int32 Rotation = 0; Rotation < 2; ++Rotation)
	{
		Item.Rotated = Rotation != 0;
		for (int32 Y = 0; Y < Candidate.Height; ++Y)
			for (int32 X = 0; X < Candidate.Width; ++X)
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
	for (const auto& I : Container.Items)
	{
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
	if (!D || Request.Slot < -1 || Request.Slot > 3)
	{
		Error = TEXT("目标位置无效");
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
	else if (Request.AutoPlace && Request.Slot == -1) Placed = AutoPlace(Target, Moving);
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
