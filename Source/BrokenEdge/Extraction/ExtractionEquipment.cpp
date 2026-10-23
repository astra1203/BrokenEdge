#include "ExtractionItems.h"

TArray<ExtractionItems::FEquipmentAction> ExtractionItems::EquipmentActions(const FExtractionContainer& C, FGuid Selected)
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
	if (D->Kind == EExtractionItemKind::Attachment)
		for (const auto& Gun : C.Items)
			if (const auto* GunDef = UExtractionItemDefinition::Get(Gun.Definition); GunDef && GunDef->SupportsAttachment(Item->Definition))
				Add(FString::Printf(TEXT("安装到 %s · %s"), *GunDef->Label.ToString(), Gun.Slot == 0 ? TEXT("手持") : Gun.Slot == 1 ? TEXT("背负") : *Gun.Id.ToString().Left(6)), EExtractionItemOp::Attach).Weapon = Gun.Id;
	if (D->Kind == EExtractionItemKind::Weapon)
	{
		Add(TEXT("卸出弹匣弹药"), EExtractionItemOp::Unload);
		for (const FName AmmoId : D->CompatibleAmmo)
			if (D->SupportsAmmo(AmmoId))
				Add(TEXT("选择 ") + UExtractionItemDefinition::Get(AmmoId)->Label.ToString(), EExtractionItemOp::SelectAmmo).AmmoDefinition = AmmoId;
		for (const auto& Part : Item->Attachments)
			if (const auto* Definition = UExtractionItemDefinition::Get(Part.Definition))
				Add(TEXT("拆下 ") + Definition->Label.ToString(), EExtractionItemOp::Detach).AttachmentSlot = Definition->AttachmentSlot;
	}
	return Actions;
}

namespace
{
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

bool ExtractionItems::ValidateItem(const FExtractionItem& I, bool Provenance, FString& Error)
{
	const auto* D = UExtractionItemDefinition::Get(I.Definition);
	if (!D || !I.Id.IsValid() || I.Size != D->Size || I.Quantity <= 0 || I.Quantity > D->MaxStack
		|| I.Magazine < 0 || I.Magazine > (D->Kind == EExtractionItemKind::Weapon ? D->MagazineCapacity : 0)
		|| !FMath::IsFinite(I.Durability) || I.Durability < 0 || I.Durability > D->MaxDurability
		|| (D->Kind != EExtractionItemKind::Weapon && !I.Attachments.IsEmpty())
		|| (Provenance && (!OriginsValid(I.Origins, I.Quantity) || !OriginsValid(I.MagazineOrigins, I.Magazine))))
	{ Error = TEXT("物品状态或来源无效"); return false; }
	if (D->Kind == EExtractionItemKind::Weapon)
	{
		const auto* Ammo = UExtractionItemDefinition::Get(I.MagazineAmmo);
		if (!Ammo || !D->SupportsAmmo(I.MagazineAmmo)) { Error = TEXT("弹匣弹种无效"); return false; }
	}
	TSet<EExtractionAttachmentSlot> Slots;
	TSet<FGuid> Ids;
	Ids.Add(I.Id);
	for (const auto& P : I.Attachments)
	{
		const auto* Part = UExtractionItemDefinition::Get(P.Definition);
		if (!Part || Part->Kind != EExtractionItemKind::Attachment || Part->AttachmentSlot == EExtractionAttachmentSlot::None
			|| !D->SupportsAttachment(P.Definition) || Slots.Contains(Part->AttachmentSlot) || !P.Id.IsValid() || Ids.Contains(P.Id)
			|| (Provenance && !OriginsValid(P.Origins, 1)))
		{ Error = TEXT("附件装配或来源无效"); return false; }
		Slots.Add(Part->AttachmentSlot); Ids.Add(P.Id);
	}
	return true;
}

FExtractionItem ExtractionItems::Unmount(const FExtractionAttachment& Part)
{
	FExtractionItem Item = Make(Part.Definition);
	Item.Id = Part.Id; Item.Origins = Part.Origins;
	return Item;
}

bool ExtractionItems::Configure(FExtractionContainer& Container, const FExtractionItemRequest& R, FString& Error)
{
	if (R.FromLoot || R.ToLoot) { Error = TEXT("请先将物品放入背包"); return false; }
	FExtractionContainer Next = Container;
	auto* Gun = Next.Find(R.Weapon.IsValid() ? R.Weapon : R.Item);
	const auto* Def = Gun ? UExtractionItemDefinition::Get(Gun->Definition) : nullptr;
	if (!Def || Def->Kind != EExtractionItemKind::Weapon) { Error = TEXT("请选择自己的枪械"); return false; }
	const FGuid GunId = Gun->Id;
	if (R.Op == EExtractionItemOp::Attach)
	{
		const auto* Part = Next.Find(R.Item);
		const auto* D = Part ? UExtractionItemDefinition::Get(Part->Definition) : nullptr;
		if (!D || !Def->SupportsAttachment(Part->Definition) || GunId == R.Item)
		{ Error = TEXT("附件不兼容"); return false; }
		for (const auto& Mounted : Gun->Attachments)
			if (UExtractionItemDefinition::Get(Mounted.Definition)->AttachmentSlot == D->AttachmentSlot)
			{ Error = TEXT("附件槽已占用，请先拆下"); return false; }
		FExtractionAttachment Mounted; Mounted.Id = Part->Id; Mounted.Definition = Part->Definition; Mounted.Origins = Part->Origins;
		Gun->Attachments.Add(Mounted);
		Next.Items.RemoveAll([&](const auto& I) { return I.Id == R.Item; });
	}
	else if (R.Op == EExtractionItemOp::Detach)
	{
		const int32 Index = Gun->Attachments.IndexOfByPredicate([&](const auto& P)
		{ return UExtractionItemDefinition::Get(P.Definition)->AttachmentSlot == R.AttachmentSlot; });
		if (Index == INDEX_NONE) { Error = TEXT("该附件槽为空"); return false; }
		auto Part = Unmount(Gun->Attachments[Index]);
		Gun->Attachments.RemoveAt(Index);
		if (!AutoPlace(Next, Part)) { Error = TEXT("背包空间不足，无法拆下"); return false; }
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
	if (!Validate(Next, Error)) return false;
	++Next.Revision; Container = MoveTemp(Next); return true;
}

float ExtractionItems::ScatterScale(const FExtractionItem& Weapon)
{
	float Result = 1;
	for (const auto& Part : Weapon.Attachments)
		if (const auto* D = UExtractionItemDefinition::Get(Part.Definition)) Result *= D->ScatterScale;
	return Result;
}

FString ExtractionItems::Details(const FExtractionItem& Item)
{
	const auto* D = UExtractionItemDefinition::Get(Item.Definition);
	if (!D) return TEXT("?");
	if (D->Kind == EExtractionItemKind::Armor) return FString::Printf(TEXT("%d级 %.0f/%.0f"), D->ArmorLevel, Item.Durability, D->MaxDurability);
	if (D->Kind == EExtractionItemKind::Weapon)
	{
		const auto* Ammo = UExtractionItemDefinition::Get(Item.MagazineAmmo);
		return FString::Printf(TEXT("%s %d/%d · 配件%d"), Ammo ? *Ammo->Label.ToString() : TEXT("?"),
			Item.Magazine, D->MagazineCapacity, Item.Attachments.Num());
	}
	return FString::Printf(TEXT("× %d"), Item.Quantity);
}
