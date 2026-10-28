#include "ExtractionItems.h"
#include "BrokenEdge/Weapon/WeaponMechanics.h"
// 物品装备规则的实现：校验耐久、弹药、附件树及精调值，并在容器副本上装卸和改装。
// 场景武器生成由 InventoryComponent 负责，这里只变更物品数据；成功返回后还需同步场景表现。

// 根据当前容器枚举穿脱护甲、装卸附件、弹种及精调操作；这里只生成候选请求，执行时仍需验证兼容性和空间。
TArray<ExtractionItems::FEquipmentAction> ExtractionItems::EquipmentActions(const FExtractionContainer& C, FGuid Selected, bool Detailed)
{
	TArray<FEquipmentAction> Actions;
	const auto* Item = C.Find(Selected);
	const auto* D = Item ? UExtractionItemDefinition::Get(Item->Definition) : nullptr;
	if (!D) return Actions;
	auto Add = [&](const FString& Label, EExtractionItemOp Op) -> FExtractionItemRequest&
	{
		FEquipmentAction A; A.Label = Label; A.Request.Item = Selected; A.Request.Op = Op;
		Actions.Add(A); return Actions.Last().Request;
	};
	if (D->Kind == EExtractionItemKind::Armor && C.WeaponSlots)
		Add(Item->Slot >= 0 ? TEXT("卸下护甲") : TEXT("穿戴护甲"), EExtractionItemOp::Transfer).Slot = Item->Slot >= 0 ? -1 : D->ArmorSlot;
	// 胸挂/背包/安全箱：取下需要先清空（Transfer 会拒绝），所以只在空的时候给"卸下"。
	if (D->Kind == EExtractionItemKind::Gear && C.GearGrids)
	{
		const bool Worn = Item->Slot == D->GearSlot;
		const bool Empty = !C.Items.ContainsByPredicate([&](const FExtractionItem& I)
		{
			return I.Slot == ExtractionGear::GridForSlot(D->GearSlot);
		});
		if (!Worn) Add(FString::Printf(TEXT("装备 %s"), *D->Label.ToString()), EExtractionItemOp::Transfer).Slot = D->GearSlot;
		else if (Empty) Add(FString::Printf(TEXT("卸下 %s"), *D->Label.ToString()), EExtractionItemOp::Transfer).Slot = -1;
	}
	if (D->Kind == EExtractionItemKind::Attachment)
		for (const auto& Gun : C.Items)
			if (const auto* GunDef = UExtractionItemDefinition::Get(Gun.Definition); GunDef && GunDef->SupportsAttachment(Item->Definition))
			{
				auto Mount = [&](const UExtractionItemDefinition* Host, FGuid Parent)
				{
					TArray<FName> Sockets; Host->MountSockets.GetKeys(Sockets); Sockets.Sort(FNameLexicalLess());
					if (Sockets.IsEmpty()) Sockets.Add(NAME_None); // Legacy root mounting remains valid.
					for (FName Socket : Sockets)
					{
						if (!Socket.IsNone() && Host->MountSockets[Socket] != D->AttachmentSlot) continue;
						auto& R = Add(FString::Printf(TEXT("安装 %s → %s %s"), *D->Label.ToString(), *Host->Label.ToString(),
							Socket.IsNone() ? TEXT("") : *Socket.ToString()), EExtractionItemOp::Attach);
						R.Weapon = Gun.Id; R.ParentAttachment = Parent; R.AttachmentSocket = Socket;
					}
				};
				if (!D->RequiresParent) Mount(GunDef, {});
				for (const auto& Parent : Gun.Attachments)
					if (const auto* PD = UExtractionItemDefinition::Get(Parent.Definition); PD && PD->ChildAttachments.Contains(Item->Definition))
					{
						Mount(PD, Parent.Id);
					}
			}
	if (D->Kind == EExtractionItemKind::Weapon)
	{
		if (!Detailed)
		{
			Add(TEXT("改装与参数对比…"), EExtractionItemOp::Tune);
			Actions.Last().OpenGunsmith = true;
		}
		Add(TEXT("卸出弹匣弹药"), EExtractionItemOp::Unload);
		for (const FName AmmoId : D->CompatibleAmmo)
			if (D->SupportsAmmo(AmmoId))
				Add(TEXT("选择 ") + UExtractionItemDefinition::Get(AmmoId)->Label.ToString(), EExtractionItemOp::SelectAmmo).AmmoDefinition = AmmoId;
		for (const auto& Part : Item->Attachments)
			if (const auto* Definition = UExtractionItemDefinition::Get(Part.Definition))
			{
				auto& Remove = Add(TEXT("拆下 ") + Definition->Label.ToString(), EExtractionItemOp::Detach);
				Remove.AttachmentSlot = Definition->AttachmentSlot; Remove.MountedAttachment = Part.Id;
				if (Detailed) for (const auto& Tune : Definition->Tunes)
				{
					if (!Tune.Curve) continue;
					const auto* Current = Part.TuneValues.FindByPredicate([&](const auto& V) { return V.Key == Tune.Id; });
					const float Value = Current ? Current->Value : Tune.Default;
					const float Step = Tune.Step > 0 ? Tune.Step : (Tune.Maximum - Tune.Minimum) / 20.f;
					for (int32 Direction : {-1, 0, 1})
					{
						const float Next = Direction == 0 ? Tune.Default : FMath::Clamp(Value + Direction * Step, Tune.Minimum, Tune.Maximum);
						if (FMath::IsNearlyEqual(Next, Value)) continue;
						auto& R = Add(FString::Printf(TEXT("%s · %s %s %.0f"), *Definition->Label.ToString(), *Tune.Id.ToString(),
							Direction == 0 ? TEXT("恢复默认") : TEXT("调至"), Next), EExtractionItemOp::Tune);
						R.MountedAttachment = Part.Id; R.TuneId = Tune.Id; R.TuneValue = Next;
					}
				}
			}
	}
	return Actions;
}

namespace
{
	// 检查来源批次 ID 唯一、数量为正且总数等于物品数量；持久化使用这些批次追踪转移与返还。
	bool OriginsValid(const TArray<FExtractionOrigin>& Parts, int32 Expected)
	{
		int64 Total = 0;
		TSet<FGuid> Seen;
		for (const auto& P : Parts)
		{
			if (!P.Lot.IsValid() || P.Quantity <= 0 || Seen.Contains(P.Lot)) return false;
			Seen.Add(P.Lot); Total += P.Quantity;
		}
		return Total == Expected;
	}
}

// 依据定义校验实例字段，并遍历附件父链检测重复槽、缺失父节点、循环、冲突及精调范围。
// Provenance 为 true 时还要求本体、弹匣、附件来源数量匹配，用于服务器持久化边界。
bool ExtractionItems::ValidateItem(const FExtractionItem& I, bool Provenance, FString& Error)
{
	const auto* D = UExtractionItemDefinition::Get(I.Definition);
	if (!D || !I.Id.IsValid() || I.Size != D->Size || I.Quantity <= 0 || I.Quantity > D->MaxStack
		|| I.Magazine < 0 || I.Magazine > (D->Kind == EExtractionItemKind::Weapon ? MagazineCapacity(I) : 0)
		|| !FMath::IsFinite(I.Durability) || I.Durability < 0 || I.Durability > D->MaxDurability
		|| (D->Kind != EExtractionItemKind::Weapon && !I.Attachments.IsEmpty())
		|| (Provenance && (!OriginsValid(I.Origins, I.Quantity) || !OriginsValid(I.MagazineOrigins, I.Magazine))))
	{ Error = TEXT("物品状态或来源无效"); return false; }
	if (D->Kind == EExtractionItemKind::Weapon)
	{
		const auto* Ammo = UExtractionItemDefinition::Get(I.MagazineAmmo);
		if (!Ammo || !D->SupportsAmmo(I.MagazineAmmo)) { Error = TEXT("弹匣弹种无效"); return false; }
	}
	if (D->Kind == EExtractionItemKind::Weapon && WeaponMechanics::Find(I.Definition)
		&& !WeaponMechanics::ResolveWeaponStats(I).IsValid())
	{ Error = TEXT("Unsupported or invalid weapon modifier"); return false; }
	TSet<FString> Slots;
	TSet<FGuid> Ids;
	Ids.Add(I.Id);
	for (const auto& P : I.Attachments)
	{
		const auto* Part = UExtractionItemDefinition::Get(P.Definition);
		if (!Part || Part->Kind != EExtractionItemKind::Attachment || Part->AttachmentSlot == EExtractionAttachmentSlot::None
			|| !D->SupportsAttachment(P.Definition) || Slots.Contains(P.Parent.ToString() + (P.Socket.IsNone() ? FString::FromInt(int32(Part->AttachmentSlot)) : P.Socket.ToString())) || !P.Id.IsValid() || Ids.Contains(P.Id)
			|| (Provenance && !OriginsValid(P.Origins, 1)))
		{ Error = TEXT("附件装配或来源无效"); return false; }
		Slots.Add(P.Parent.ToString() + (P.Socket.IsNone() ? FString::FromInt(int32(Part->AttachmentSlot)) : P.Socket.ToString())); Ids.Add(P.Id);
		if (!P.Parent.IsValid() && Part->RequiresParent) { Error = TEXT("Attachment requires a parent"); return false; }
		if (!P.Socket.IsNone())
		{
			const auto* Host = I.Attachments.FindByPredicate([&](const auto& N) { return N.Id == P.Parent; });
			const auto* HostDef = P.Parent.IsValid() ? (Host ? UExtractionItemDefinition::Get(Host->Definition) : nullptr) : D;
			const auto* SocketType = HostDef ? HostDef->MountSockets.Find(P.Socket) : nullptr;
			if (!SocketType || *SocketType != Part->AttachmentSlot) { Error = TEXT("Unknown or incompatible mount socket"); return false; }
		}
		FGuid Parent = P.Parent;
		// 沿父链查找时记录已访问节点，避免非法装配形成循环；父子兼容性也需逐级成立。
		TSet<FGuid> Ancestors; Ancestors.Add(P.Id);
		while (Parent.IsValid())
		{
			if (Ancestors.Contains(Parent)) { Error = TEXT("Attachment cycle"); return false; }
			Ancestors.Add(Parent);
			const auto* Node = I.Attachments.FindByPredicate([&](const auto& N) { return N.Id == Parent; });
			if (!Node) { Error = TEXT("Missing attachment parent"); return false; }
			const auto* ParentDef = UExtractionItemDefinition::Get(Node->Definition);
			if (!ParentDef || (Parent == P.Parent && !ParentDef->ChildAttachments.Contains(P.Definition)))
			{ Error = TEXT("Incompatible attachment parent"); return false; }
			Parent = Node->Parent;
		}
		for (const auto& Other : I.Attachments)
			if (Other.Id != P.Id && Part->ConflictingAttachments.Contains(Other.Definition))
			{ Error = TEXT("Conflicting attachments"); return false; }
		for (const auto& Value : P.TuneValues)
		{
			const auto* Tune = Part->Tunes.FindByPredicate([&](const auto& T) { return T.Id == Value.Key; });
			if (!Tune || !Tune->Curve || !FMath::IsFinite(Value.Value) || Value.Value < Tune->Minimum || Value.Value > Tune->Maximum
				|| (Tune->Step > 0 && !FMath::IsNearlyEqual((Value.Value - Tune->Minimum) / Tune->Step,
					FMath::RoundToFloat((Value.Value - Tune->Minimum) / Tune->Step), .001f)))
			{ Error = TEXT("Invalid attachment tuning"); return false; }
		}
	}
	return true;
}

// 把挂载附件恢复为散装物品，保留实例 ID、来源和精调，拆装不应凭空生成另一件资产。
FExtractionItem ExtractionItems::Unmount(const FExtractionAttachment& Part)
{
	FExtractionItem Item = Make(Part.Definition);
	Item.Id = Part.Id; Item.Origins = Part.Origins; Item.TuneValues = Part.TuneValues;
	return Item;
}

// 先复制容器，再处理安装、整棵子树拆卸、精调、卸弹或换弹种；最终验证通过才递增版本并提交。
bool ExtractionItems::Configure(FExtractionContainer& Container, const FExtractionItemRequest& R, FString& Error)
{
	if (R.FromLoot || R.ToLoot) { Error = TEXT("请先将物品放入背包"); return false; }
	FExtractionContainer Next = Container;
	auto* Gun = Next.Find(R.Weapon.IsValid() ? R.Weapon : R.Item);
	const auto* Def = Gun ? UExtractionItemDefinition::Get(Gun->Definition) : nullptr;
	if (!Def || Def->Kind != EExtractionItemKind::Weapon) { Error = TEXT("请选择自己的枪械"); return false; }
	const FGuid GunId = Gun->Id;
	const int32 OldCapacity = MagazineCapacity(*Gun);
	if (R.Op == EExtractionItemOp::Attach)
	{
		const auto* Part = Next.Find(R.Item);
		const auto* D = Part ? UExtractionItemDefinition::Get(Part->Definition) : nullptr;
		if (!D || !Def->SupportsAttachment(Part->Definition) || GunId == R.Item)
		{ Error = TEXT("附件不兼容"); return false; }
		for (const auto& Mounted : Gun->Attachments)
			if (Mounted.Parent == R.ParentAttachment && Mounted.Socket == R.AttachmentSocket && UExtractionItemDefinition::Get(Mounted.Definition)->AttachmentSlot == D->AttachmentSlot)
			{ Error = TEXT("附件槽已占用，请先拆下"); return false; }
		FExtractionAttachment Mounted; Mounted.Id = Part->Id; Mounted.Definition = Part->Definition; Mounted.Origins = Part->Origins; Mounted.Parent = R.ParentAttachment; Mounted.Socket = R.AttachmentSocket; Mounted.TuneValues = Part->TuneValues;
		Gun->Attachments.Add(Mounted);
		Next.Items.RemoveAll([&](const auto& I) { return I.Id == R.Item; });
	}
	else if (R.Op == EExtractionItemOp::Detach)
	{
		const int32 Index = Gun->Attachments.IndexOfByPredicate([&](const auto& P)
		{ return R.MountedAttachment.IsValid() ? P.Id == R.MountedAttachment : !P.Parent.IsValid() && UExtractionItemDefinition::Get(P.Definition)->AttachmentSlot == R.AttachmentSlot; });
		if (Index == INDEX_NONE) { Error = TEXT("该附件槽为空"); return false; }
		// 拆父件时收集所有后代，整组转为散件；任何一件放不进背包都放弃候选改动。
		TSet<FGuid> Removed; Removed.Add(Gun->Attachments[Index].Id);
		bool Added = true;
		while (Added)
		{
			Added = false;
			for (const auto& P : Gun->Attachments)
				if (Removed.Contains(P.Parent) && !Removed.Contains(P.Id)) { Removed.Add(P.Id); Added = true; }
		}
		TArray<FExtractionItem> Detached;
		for (const auto& P : Gun->Attachments) if (Removed.Contains(P.Id)) Detached.Add(Unmount(P));
		Gun->Attachments.RemoveAll([&](const auto& P) { return Removed.Contains(P.Id); });
		// AutoPlace may reallocate Items; never retain Gun after this point.
		for (auto& Part : Detached) if (!AutoPlace(Next, Part)) { Error = TEXT("背包空间不足，无法拆下"); return false; }
	}
	else if (R.Op == EExtractionItemOp::Tune)
	{
		auto* Part = Gun->Attachments.FindByPredicate([&](const auto& P) { return P.Id == R.MountedAttachment; });
		if (!Part) { Error = TEXT("Missing tuning target"); return false; }
		Part->TuneValues.RemoveAll([&](const auto& T) { return T.Key == R.TuneId; });
		FWeaponTuneValue Value; Value.Key = R.TuneId; Value.Value = R.TuneValue;
		Part->TuneValues.Add(Value);
	}
	else if (R.Op == EExtractionItemOp::Unload)
	{
		if (Gun->Magazine <= 0) { Error = TEXT("弹匣已空"); return false; }
		auto Ammo = Make(Gun->MagazineAmmo, Gun->Magazine);
		Ammo.Origins = Gun->MagazineOrigins;
		Gun->Magazine = 0; Gun->MagazineOrigins.Reset();
		if (!AutoPlace(Next, Ammo)) { Error = TEXT("背包空间不足，无法卸弹"); return false; }
	}
	else if (R.Op == EExtractionItemOp::SelectAmmo)
	{
		const auto* Ammo = UExtractionItemDefinition::Get(R.AmmoDefinition);
		if (!Ammo || !Def->SupportsAmmo(R.AmmoDefinition)) { Error = TEXT("弹种不兼容"); return false; }
		if (Gun->Magazine && Gun->MagazineAmmo != R.AmmoDefinition) { Error = TEXT("更换弹种前请先卸弹"); return false; }
		Gun->MagazineAmmo = R.AmmoDefinition;
	}
	else { Error = TEXT("无效装备操作"); return false; }
	if (const auto* Updated = Next.Find(GunId); Updated && Updated->Magazine > 0 && MagazineCapacity(*Updated) < OldCapacity)
	{ Error = TEXT("Unload ammunition before reducing magazine capacity"); return false; }
	// 所有改装分支最终汇合到完整容器校验，只有通过后才替换调用者数据。
	if (!Validate(Next, Error)) return false;
	++Next.Revision; Container = MoveTemp(Next); return true;
}

// 组合已安装附件的散布倍率；完整武器属性解析由 WeaponMechanics 提供。
float ExtractionItems::ScatterScale(const FExtractionItem& Weapon)
{
	float Result = 1;
	for (const auto& Part : Weapon.Attachments)
		if (const auto* D = UExtractionItemDefinition::Get(Part.Definition)) Result *= D->ScatterScale;
	return Result;
}

// 生成护甲、武器或普通堆叠的简要展示文本，不作为权威数据来源。
FString ExtractionItems::Details(const FExtractionItem& Item)
{
	const auto* D = UExtractionItemDefinition::Get(Item.Definition);
	if (!D) return TEXT("?");
	if (D->Kind == EExtractionItemKind::Armor) return FString::Printf(TEXT("%d级 %.0f/%.0f"), D->ArmorLevel, Item.Durability, D->MaxDurability);
	if (D->Kind == EExtractionItemKind::Weapon)
	{
		const auto* Ammo = UExtractionItemDefinition::Get(Item.MagazineAmmo);
		return FString::Printf(TEXT("%s %d/%d · 配件%d"), Ammo ? *Ammo->Label.ToString() : TEXT("?"),
			Item.Magazine, MagazineCapacity(Item), Item.Attachments.Num());
	}
	return FString::Printf(TEXT("× %d"), Item.Quantity);
}

// 读取解析后的武器参数用于 UI，对单位做换算，并明确这些是项目兼容参数。
TArray<FString> ExtractionItems::WeaponDetails(const FExtractionItem& Item)
{
	const auto S = WeaponMechanics::ResolveWeaponStats(Item);
	if (!S.IsValid()) return {};
	TArray<FString> Lines;
	const auto* Gun = UExtractionItemDefinition::Get(Item.Definition);
	Lines.Add(Item.Definition == TEXT("AK")
		          ? FString(TEXT("AKM · 枪械参数"))
		          : (Gun ? Gun->Label.ToString() : Item.Definition.ToString()) + TEXT(" · 枪械参数"));
	Lines.Add(FString::Printf(TEXT("射速 %.1f 发/分    初速 %.0f 米/秒"), 60 / S.Interval, S.Speed / 100));
	Lines.Add(FString::Printf(TEXT("开镜 %.0f 毫秒    跑射延迟 %.0f 毫秒"), S.AimIn * 1000, S.SprintToFire * 1000));
	Lines.Add(FString::Printf(TEXT("换弹 %.2f 秒    空仓 %.2f 秒"), S.Reload, S.EmptyReload));
	Lines.Add(FString::Printf(TEXT("弹匣 %.0f 发    后座倍率 %.2f    散布倍率 %.2f"), S.MagazineCapacity, S.RecoilScale, S.SpreadScale));
	Lines.Add(TEXT("项目兼容参数 · 不代表正式服评分"));
	Lines.Add(TEXT("装配结构（右键选择配件进行安装/拆卸）"));
	for (const auto& Part : Item.Attachments)
	{
		const auto* D = UExtractionItemDefinition::Get(Part.Definition);
		Lines.Add((Part.Parent.IsValid() ? TEXT("    └ ") : TEXT("  ├ ")) + (D ? D->Label.ToString() : Part.Definition.ToString()));
	}
	return Lines;
}

// 优先使用装配后解析的容量，解析不可用时退回定义容量，未知定义返回零。
int32 ExtractionItems::MagazineCapacity(const FExtractionItem& Item)
{
	const auto Stats = WeaponMechanics::ResolveWeaponStats(Item);
	const auto* Definition = UExtractionItemDefinition::Get(Item.Definition);
	return Stats.IsValid() ? FMath::RoundToInt(Stats.MagazineCapacity) : Definition ? Definition->MagazineCapacity : 0;
}
