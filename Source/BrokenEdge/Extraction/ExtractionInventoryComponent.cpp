#include "ExtractionInventoryComponent.h"
#include "BrokenEdge/Weapon/WeaponMechanics.h"
#include "ExtractionCombatComponent.h"
#include "ExtractionAbilities.h"
#include "ExtractionState.h"
#include "ExtractionLootActor.h"
#include "ExtractionPersistence.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "BrokenEdge/BrokenEdgeTypes/WeaponType.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/Class.h"
// 局内物品的权威容器：玩家挂在 PlayerState 上，守卫可挂在角色上。
// Contents 保存物品实例，武器 Actor 是其场景表现；操作时要同步两者，不能只改变可见枪械。
// 背包内容只复制给拥有者，来源批次用于服务器的消耗、转移和崩溃恢复。

// 启用组件复制；具体哪些字段复制及发给谁由 GetLifetimeReplicatedProps 指定。
UExtractionInventoryComponent::UExtractionInventoryComponent() { SetIsReplicatedByDefault(true); }

namespace
{
	bool SameContainer(const FExtractionContainer& A, const FExtractionContainer& B)
	{
		return FExtractionContainer::StaticStruct()->CompareScriptStruct(&A, &B, 0);
	}
	bool PrepareRevision(FExtractionContainer& Candidate, const FExtractionContainer& Before)
	{
		if (Candidate.Revision < Before.Revision) return false;
		if (Candidate.Revision == Before.Revision && !SameContainer(Candidate, Before))
		{
			if (Before.Revision == MAX_int32) return false;
			++Candidate.Revision;
		}
		return true;
	}
}

FExtractionInventoryResult UExtractionInventoryComponent::Mutate(
	TFunctionRef<bool(FExtractionContainer&, FString&)> Edit, int32 ExpectedRevision, bool bSyncWeapons)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
		return {EExtractionInventoryCommit::Rejected, TEXT("Inventory mutation requires authority")};
	if (ExpectedRevision != INDEX_NONE && ExpectedRevision != Contents.Revision)
		return {EExtractionInventoryCommit::Stale, TEXT("Inventory revision changed")};
	FExtractionContainer Candidate = Contents;
	FString Error;
	if (!Edit(Candidate, Error)) return {EExtractionInventoryCommit::Rejected, Error};
	return CommitCandidate(MoveTemp(Candidate), bSyncWeapons);
}

FExtractionInventoryResult UExtractionInventoryComponent::ConsumeOne(FGuid ItemId)
{
	return Mutate([&](FExtractionContainer& Candidate, FString& Error)
	{
		auto* Item = Candidate.Find(ItemId);
		const auto* Definition = Item ? UExtractionItemDefinition::Get(Item->Definition) : nullptr;
		if (!Item || Item->Quantity <= 0 || !Definition ||
			(Definition->Kind != EExtractionItemKind::Medical && Definition->Kind != EExtractionItemKind::Throwable))
		{
			Error = TEXT("物品已变化或无法消耗");
			return false;
		}
		ExtractionItems::TakeOrigins(Item->Origins, 1);
		if (--Item->Quantity == 0)
			Candidate.Items.RemoveAll([&](const FExtractionItem& Entry) { return Entry.Id == ItemId; });
		return true;
	}, INDEX_NONE, false);
}

FExtractionInventoryResult UExtractionInventoryComponent::MutatePair(UExtractionInventoryComponent& Inventory,
	AExtractionLootActor& Loot, TFunctionRef<bool(FExtractionContainer&, FExtractionContainer&, FString&)> Edit,
	int32 ExpectedInventoryRevision, int32 ExpectedLootRevision)
{
	if (!Inventory.GetOwner() || !Inventory.GetOwner()->HasAuthority() || !Loot.HasAuthority()
		|| Inventory.GetWorld() != Loot.GetWorld())
		return {EExtractionInventoryCommit::Rejected, TEXT("Pair mutation requires authority in the same world")};
	if ((ExpectedInventoryRevision != INDEX_NONE && ExpectedInventoryRevision != Inventory.Contents.Revision)
		|| (ExpectedLootRevision != INDEX_NONE && ExpectedLootRevision != Loot.Contents.Revision))
		return {EExtractionInventoryCommit::Stale, TEXT("Container revision changed")};
	FExtractionContainer Candidate = Inventory.Contents, Other = Loot.Contents;
	FString Error;
	if (!Edit(Candidate, Other, Error)) return {EExtractionInventoryCommit::Rejected, Error};
	return Inventory.CommitCandidate(MoveTemp(Candidate), true, &Loot, &Other);
}

FExtractionInventoryResult UExtractionInventoryComponent::CommitCandidate(FExtractionContainer Candidate,
	bool bSyncWeapons, AExtractionLootActor* Loot, FExtractionContainer* OtherCandidate)
{
	const bool bInventoryChanged = !SameContainer(Candidate, Contents);
	const bool bOtherChanged = Loot && !SameContainer(*OtherCandidate, Loot->Contents);
	if (!bInventoryChanged && !bOtherChanged) return {EExtractionInventoryCommit::NoChange, {}};
	if (!PrepareRevision(Candidate, Contents) || (Loot && !PrepareRevision(*OtherCandidate, Loot->Contents)))
		return {EExtractionInventoryCommit::Rejected, TEXT("Invalid container revision")};
	const FExtractionContainer Before = Contents;
	FExtractionContainer OtherBefore;
	Contents = MoveTemp(Candidate);
	if (Loot) { OtherBefore = Loot->Contents; Loot->Contents = MoveTemp(*OtherCandidate); }
	if (bSyncWeapons && !SyncWeapons())
	{
		Contents = Before;
		if (Loot) Loot->Contents = MoveTemp(OtherBefore);
		if (!SyncWeapons())
			UE_LOG(LogTemp, Error, TEXT("EXTRACTION_INVENTORY_ROLLBACK_SYNC_FAILED Owner=%s"), *GetNameSafe(GetOwner()));
		return {EExtractionInventoryCommit::WeaponSyncFailed, TEXT("Weapon synchronization failed")};
	}
	if (bInventoryChanged) Changed();
	if (bOtherChanged) Loot->Changed();
	return {EExtractionInventoryCommit::Ok, {}};
}

// 背包 Contents 仅复制给所属客户端，避免把玩家完整库存广播给所有人。
void UExtractionInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UExtractionInventoryComponent, Contents, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UExtractionInventoryComponent, bHoldingSecondary, COND_OwnerOnly);
}

// 想拿副武器但副武器槽空时退回主武器槽，反之亦然；有枪就一定握着一把。
int32 UExtractionInventoryComponent::HeldSlot() const
{
	const bool Has0 = Contents.InSlot(0) != nullptr, Has1 = Contents.InSlot(1) != nullptr;
	if (bHoldingSecondary) return Has1 ? 1 : Has0 ? 0 : -1;
	return Has0 ? 0 : Has1 ? 1 : -1;
}

// 服务器改手持槽后重新分配手持/背负的武器 Actor。
void UExtractionInventoryComponent::SetHeldSlot(int32 Slot)
{
	if (!GetOwner()->HasAuthority()) return;
	bHoldingSecondary = Slot == 1;
	SyncWeapons();
	GetOwner()->ForceNetUpdate();
}

// 玩家通过 PlayerState 找当前 Pawn；守卫组件直接取所属角色，统一后续装备操作入口。
ABrokenEdgeCharacter* UExtractionInventoryComponent::Character() const
{
	const auto* PS = Cast<AExtractionPlayerState>(GetOwner());
	return PS ? Cast<ABrokenEdgeCharacter>(PS->GetPawn()) : Cast<ABrokenEdgeCharacter>(GetOwner());
}

// 服务器从出战票据中的 Original 初始化携带装备；用世界标识防止同一世界重复发放。
// 补给放不下时在附近安全地面生成容器，然后同步枪械 Actor 和背包版本。
void UExtractionInventoryComponent::InitializeLoadout()
{
	if (!GetOwner()->HasAuthority() || !Character() || (bInitialized && InitializedWorld == GetWorld())) return;
	const auto* PS = Cast<AExtractionPlayerState>(GetOwner());
	const auto* Store = GetWorld()->GetGameInstance()->GetSubsystem<UExtractionPersistence>();
	const auto* Raid = PS ? Store->FindRaid(PS->ExtractionRaid) : nullptr;
	const auto* Member = Raid
		                     ? Raid->Players.FindByPredicate([&](const auto& P)
		                     {
			                     return P.Profile == PS->ExtractionProfile;
		                     })
		                     : nullptr;
	if (!Member || Member->Outcome != EExtractionOutcome::None) return;
	FExtractionContainer Candidate = Member->Original;
	Candidate.Revision = FMath::Max(Candidate.Revision, Contents.Revision);
	Candidate.Id = FGuid::NewGuid();
	AExtractionLootActor* Supply = nullptr;
	// 补给生成是独立于原出战装备的局内规则；容器装不下时保留为地面物资。
	const FExtractionContainer Overflow = GrantStartingSupply(Candidate);
	if (!Overflow.Items.IsEmpty())
	{
		auto* C = Character();
		FVector Position = C->GetActorLocation() - FVector(0, 0, 70);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(StartingSupply), false, C);
		for (TActorIterator<APawn> It(GetWorld()); It; ++It) Query.AddIgnoredActor(*It);
		for (TActorIterator<AWeapon> It(GetWorld()); It; ++It) Query.AddIgnoredActor(*It);
		for (TActorIterator<AExtractionLootActor> It(GetWorld()); It; ++It) Query.AddIgnoredActor(*It);
		for (int32 Side : {1, -1, 0})
		{
			const FVector SupplyPosition = C->GetActorLocation() + C->GetActorRightVector() * Side * 100;
			FHitResult Ground, Wall;
			if (!GetWorld()->LineTraceSingleByChannel(Wall, C->GetActorLocation(), SupplyPosition, ECC_Visibility, Query)
				&& GetWorld()->LineTraceSingleByChannel(Ground, SupplyPosition, SupplyPosition - FVector(0, 0, 200),
				                                        ECC_Visibility, Query)
				&& Ground.ImpactNormal.Z > .7f)
			{
				Position = Ground.ImpactPoint + FVector(0, 0, 20);
				break;
			}
		}
		if ((Supply = AExtractionLootActor::SpawnContainer(GetWorld(), Position, false)))
		{
			Supply->Label = TEXT("开局补给");
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("EXTRACTION_STARTING_SUPPLY_SPAWN_FAILED"));
			return;
		}
	}
	FExtractionContainer Other = Overflow;
	const auto Result = CommitCandidate(MoveTemp(Candidate), true, Supply, Supply ? &Other : nullptr);
	if (!Result.Ok())
	{
		if (Supply) Supply->Destroy();
		UE_LOG(LogTemp, Error, TEXT("EXTRACTION_LOADOUT_COMMIT_FAILED %s"), *Result.Error);
		return;
	}
	bInitialized = true;
	InitializedWorld = GetWorld();
	bDropped = false;
}

// 开局测试配装：先为两把测试长枪与备弹预留空间，再装回原有物品。
// 所有操作在副本中完成；原装备的 GUID、附件、弹匣与来源批次随物品保留。
FExtractionContainer UExtractionInventoryComponent::GrantStartingSupply(FExtractionContainer& Inventory)
{
	FExtractionContainer Overflow;
	Overflow.Id = FGuid::NewGuid();
	Overflow.Width = Overflow.Height = 16;
	for (FName Id : {
		     TEXT("Bag_Test"), TEXT("Helmet2"), TEXT("Vest2"), TEXT("M4A1"), TEXT("SCARH"),
		     TEXT("BoltSniper"), TEXT("PumpShotgun"), TEXT("Ammo55645"), TEXT("Ammo76251"),
		     TEXT("Ammo_338"), TEXT("Ammo_12G"), TEXT("Rig_Assault"), TEXT("FragGrenade")
	     })
		if (!UExtractionItemDefinition::Get(Id))
		{
			UE_LOG(LogTemp, Error, TEXT("EXTRACTION_STARTING_SUPPLY_MISSING %s"), *Id.ToString());
			return Overflow;
		}
	FExtractionContainer Next = Inventory;
	Next.WeaponSlots = Next.GearGrids = true;
	Next.PersonalPockets = true;
	TArray<FExtractionItem> Pending;
	for (const auto& Item : Inventory.Items)
		if (Item.Slot == ExtractionGear::BagGrid) Pending.Add(Item);
	Next.Items.RemoveAll([](const FExtractionItem& Item) { return Item.Slot == ExtractionGear::BagGrid; });
	auto RemoveSlot = [&](int32 Slot)
	{
		if (const auto* Item = Next.InSlot(Slot))
		{
			Pending.Add(*Item);
			Next.Items.RemoveAll([&](const FExtractionItem& I) { return I.Slot == Slot; });
		}
	};
	auto TakeOrMake = [&](FName Id)
	{
		const int32 Index = Pending.IndexOfByPredicate([&](const FExtractionItem& I) { return I.Definition == Id; });
		if (Index == INDEX_NONE) return ExtractionItems::Make(Id);
		const FExtractionItem Item = Pending[Index];
		Pending.RemoveAt(Index);
		return Item;
	};
	if (const auto* Bag = Next.InSlot(ExtractionGear::BagSlot); !Bag || Bag->Definition != TEXT("Bag_Test"))
	{
		RemoveSlot(ExtractionGear::BagSlot);
		auto Item = TakeOrMake(TEXT("Bag_Test"));
		Item.Slot = ExtractionGear::BagSlot;
		Next.Items.Add(Item);
	}
	if (!Next.InSlot(ExtractionGear::RigSlot))
	{
		auto Item = TakeOrMake(TEXT("Rig_Assault"));
		Item.Slot = ExtractionGear::RigSlot;
		Next.Items.Add(Item);
	}
	for (const auto& Pair : {TPair<int32, FName>(2, TEXT("Helmet2")), TPair<int32, FName>(3, TEXT("Vest2"))})
		if (const auto* Gear = Next.InSlot(Pair.Key); !Gear || Gear->Durability <= 0.f)
		{
			RemoveSlot(Pair.Key);
			auto Item = ExtractionItems::Make(Pair.Value);
			Item.Slot = Pair.Key;
			Next.Items.Add(Item);
		}
	const TPair<int32, FName> Guns[] = {{0, TEXT("M4A1")}, {1, TEXT("SCARH")}};
	for (const auto& [Slot, Gun] : Guns)
	{
		const auto* Def = UExtractionItemDefinition::Get(Gun);
		if (!Def) continue;
		if (const auto* Current = Next.InSlot(Slot); Current && Current->Definition != Gun) RemoveSlot(Slot);
		if (!Next.InSlot(Slot))
		{
			auto Item = TakeOrMake(Gun);
			Item.Slot = Slot;
			Next.Items.Add(Item);
		}
		// 测试配件：没装任何配件的出生枪按瞄具、握把、枪口各装一件兼容配件（取兼容列表里第一件可直接装在枪身上的）。
		if (const auto* Held = Next.InSlot(Slot))
			if (auto* Mount = Next.Find(Held->Id); Mount && Mount->Attachments.IsEmpty())
				for (const EExtractionAttachmentSlot Want : {
					     EExtractionAttachmentSlot::Optic, EExtractionAttachmentSlot::Grip,
					     EExtractionAttachmentSlot::Muzzle
				     })
				{
					FName Socket = NAME_None;
					bool bHasSocket = Def->MountSockets.IsEmpty();
					for (const auto& [Name, Type] : Def->MountSockets)
						if (Type == Want)
						{
							Socket = Name;
							bHasSocket = true;
							break;
						}
					if (!bHasSocket) continue;
					for (const FName PartId : Def->CompatibleAttachments)
					{
						const auto* PartDef = UExtractionItemDefinition::Get(PartId);
						if (!PartDef || PartDef->AttachmentSlot != Want || PartDef->RequiresParent) continue;
						const FExtractionItem Part = ExtractionItems::Make(PartId);
						FExtractionAttachment Mounted;
						Mounted.Id = Part.Id;
						Mounted.Definition = PartId;
						Mounted.Origins = Part.Origins;
						Mounted.Socket = Socket;
						Mount->Attachments.Add(Mounted);
						break;
					}
				}
	}
	bool bPlaced = true;
	for (FName Gun : {TEXT("BoltSniper"), TEXT("PumpShotgun")})
	{
		auto Item = TakeOrMake(Gun);
		Item.Slot = ExtractionGear::BagGrid;
		Item.Cell = FIntPoint(0, Gun == TEXT("BoltSniper") ? 0 : 2);
		Item.Rotated = false;
		bPlaced &= ExtractionItems::Fits(Next, Item);
		Next.Items.Add(Item);
	}
	// Reuse carried grenades before creating any new instances.
	auto CarriedGrenades = [&]()
	{
		int32 Count = 0;
		for (const auto& Item : Next.Items)
			if (Item.Definition == TEXT("FragGrenade") && Item.Slot != ExtractionGear::SafeGrid) Count += Item.Quantity;
		return Count;
	};
	while (CarriedGrenades() < 2)
	{
		auto Item = TakeOrMake(TEXT("FragGrenade"));
		if (!ExtractionItems::AutoPlace(Next, Item, ExtractionGear::RigGrid)
			&& !ExtractionItems::AutoPlace(Next, Item, ExtractionGear::BagGrid))
		{
			bPlaced = false;
			break;
		}
	}
	const TPair<FName, int32> Reserve[] = {
		{TEXT("Ammo55645"), 60}, {TEXT("Ammo76251"), 60},
		{TEXT("Ammo_338"), 20}, {TEXT("Ammo_12G"), 24}
	};
	for (const auto& [Ammo, Minimum] : Reserve)
	{
		while (Next.Count(Ammo) < Minimum)
		{
			const int32 Index = Pending.IndexOfByPredicate([&](const FExtractionItem& I)
			{
				return I.Definition == Ammo;
			});
			FExtractionItem Item;
			if (Index != INDEX_NONE)
			{
				Item = Pending[Index];
				Pending.RemoveAt(Index);
			}
			else Item = ExtractionItems::Make(Ammo, Minimum - Next.Count(Ammo));
			if (!ExtractionItems::AutoPlace(Next, Item, ExtractionGear::BagGrid))
			{
				bPlaced = false;
				break;
			}
		}
	}
	// 留出一块完整的长枪格位，玩家才能先收起手中的步枪，再取出测试枪。
	// 仅在装回旧物品时占位，提交库存前移除；旧物品放不下仍进入出生补给箱。
	auto SwapSpace = ExtractionItems::Make(TEXT("BoltSniper"));
	SwapSpace.Slot = ExtractionGear::BagGrid;
	SwapSpace.Cell = FIntPoint(0, 4);
	bPlaced &= ExtractionItems::Fits(Next, SwapSpace);
	Next.Items.Add(SwapSpace);
	Pending.StableSort([](const FExtractionItem& A, const FExtractionItem& B)
	{
		return A.Size.X * A.Size.Y > B.Size.X * B.Size.Y;
	});
	for (auto Item : Pending)
		if (!ExtractionItems::AutoPlace(Next, Item, ExtractionGear::BagGrid))
			bPlaced &= ExtractionItems::AutoPlace(Overflow, Item);
	Next.Items.RemoveAll([&](const FExtractionItem& I) { return I.Id == SwapSpace.Id; });
	FString Error;
	if (!bPlaced || !ExtractionItems::Validate(Next, Error) || !ExtractionItems::Validate(Overflow, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("EXTRACTION_STARTING_SUPPLY_INVALID %s"), *Error);
		Overflow.Items.Reset();
		return Overflow;
	}
	++Next.Revision;
	Inventory = MoveTemp(Next);
	return Overflow;
}

// 为服务器守卫创建独立 AK、弹药和医疗包，不读取玩家长期档案。
void UExtractionInventoryComponent::InitializeGuard()
{
	auto* C = Character();
	if (!C || !C->HasAuthority() || !C->bExtractionGuard || bInitialized) return;
	auto Candidate = Contents;
	Candidate.Id = FGuid::NewGuid();
	Candidate.WeaponSlots = true;
	// 按配置随机配枪，弹药跟随枪的默认弹种；配置无效时回退 AK。
	const auto& Weapons = GetDefault<UExtractionRules>()->GuardWeapons;
	FName GunId = Weapons.IsEmpty() ? FName(TEXT("AK")) : Weapons[FMath::RandRange(0, Weapons.Num() - 1)];
	const auto* GunDef = UExtractionItemDefinition::Get(GunId);
	if (!GunDef || GunDef->Kind != EExtractionItemKind::Weapon)
	{
		GunId = TEXT("AK");
		GunDef = UExtractionItemDefinition::Get(GunId);
	}
	auto Gun = ExtractionItems::Make(GunId);
	Gun.Slot = 0;
	ExtractionItems::SeedOrigins(Gun);
	Candidate.Items.Add(Gun);
	ExtractionItems::AutoPlace(Candidate, ExtractionItems::Make(GunDef ? GunDef->DefaultAmmo : FName(TEXT("Ammo")), 60));
	ExtractionItems::AutoPlace(Candidate, ExtractionItems::Make(TEXT("Medkit")));
	if (CommitCandidate(MoveTemp(Candidate), true).Ok()) bInitialized = true;
}

// 结算后的清理路径：标记已处理并清空物品和武器，防止角色销毁时再次掉落已入库资产。
void UExtractionInventoryComponent::ClearSettled()
{
	if (!GetOwner()->HasAuthority()) return;
	auto Candidate = Contents;
	Candidate.Items.Reset();
	if (CommitCandidate(MoveTemp(Candidate), true).Ok()) bDropped = true;
}

// 从背包当前弹种总量刷新旧战斗组件的备弹显示，备弹不再是可独立增减的另一份库存。
void UExtractionInventoryComponent::SyncReserve()
{
	if (auto* C = Character())
	{
		auto* Combat = C->GetCombat().Get();
		const auto* Gun = HeldGun();
		Combat->CarriedAmmo = Contents.Count(Gun ? Gun->MagazineAmmo : FName(TEXT("Ammo")));
		Combat->CarriedAmmoMap.FindOrAdd(EWeaponType::EWT_AssaultRifle) = Combat->CarriedAmmo;
		Combat->UpdateCarriedAmmo();
	}
}

// 客户端收到背包快照后更新备弹显示；复制回调不执行入库或掉落。
void UExtractionInventoryComponent::OnRep_Contents() { SyncReserve(); }

// 刷新备弹并请求尽快网络更新；版本号由修改容器的调用方推进。
void UExtractionInventoryComponent::Changed()
{
	SyncReserve();
	GetOwner()->ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_INVENTORY Owner=%s Container=%s Revision=%d Items=%d Ammo=%d"),
	       *GetOwner()->GetName(), *Contents.Id.ToString(), Contents.Revision, Contents.Items.Num(),
	       Contents.Count(TEXT("Ammo")));
}

// 按物品 ID 复用或创建主副武器 Actor，再同步弹药、附件、手持和背负挂点。
// 先准备所有需要的 Actor，生成失败则销毁本次新建对象并返回 false，让上层撤回库存变更。
bool UExtractionInventoryComponent::SyncWeapons()
{
#if WITH_DEV_AUTOMATION_TESTS
	if (bFailNextWeaponSync) { bFailNextWeaponSync = false; return false; }
#endif
	auto* C = Character();
	if (!C || !C->HasAuthority()) return false;
	auto* Combat = C->GetCombat().Get();
	AWeapon* Desired[2] = {nullptr, nullptr};
	TArray<AWeapon*> Created;
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		const auto* Item = Contents.InSlot(Slot);
		if (!Item) continue;
		for (TActorIterator<AWeapon> It(GetWorld()); It; ++It)
			if (It->ExtractionItemId == Item->Id && It->GetOwner() == C)
			{
				Desired[Slot] = *It;
				break;
			}
		if (!Desired[Slot])
		{
			const auto* Def = UExtractionItemDefinition::Get(Item->Definition);
			FActorSpawnParameters Params;
			Params.Owner = C;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Desired[Slot] = Def && Def->WeaponClass
				                ? GetWorld()->SpawnActor<AWeapon>(Def->WeaponClass, C->GetActorTransform(), Params)
				                : nullptr;
			if (!Desired[Slot])
			{
				for (auto* W : Created) W->Destroy();
				return false;
			}
			Desired[Slot]->ExtractionItemId = Item->Id;
			Desired[Slot]->ExtractionDefinition = Item->Definition;
			Created.Add(Desired[Slot]);
		}
	}
	AWeapon* Old[2] = {Combat->EquippedWeapon, Combat->SecondaryWeapon};
	// 槽位固定为主/副武器；手持哪一把由 HeldSlot 决定，另一把背在身上。
	const int32 Held = FMath::Max(0, HeldSlot());
	Combat->EquippedWeapon = Desired[Held];
	Combat->SecondaryWeapon = Desired[1 - Held];
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		if (!Desired[Slot]) continue;
		const auto* State = Contents.InSlot(Slot);
		Desired[Slot]->ExtractionAmmoType = State->MagazineAmmo;
		Desired[Slot]->ExtractionParts.Reset();
		Desired[Slot]->ExtractionAssembly = State->Attachments;
		Desired[Slot]->ExtractionConfiguration = WeaponMechanics::ResolveWeaponStats(*State).Revision;
		for (const auto& Part : State->Attachments) Desired[Slot]->ExtractionParts.Add(Part.Definition);
		if (Created.Contains(Desired[Slot]) || Desired[Slot]->GetAmmo() != State->Magazine)
			C->GetExtractionCombat()->EquipmentAmmo(Desired[Slot], State->Magazine);
		Desired[Slot]->SetWeaponState(Slot == Held ? EWeaponState::EWS_Equipped : EWeaponState::EWS_EquippedSecondary);
		if (Slot == Held) Combat->AttachActorToRightHand(Desired[Slot]);
		else Combat->AttachActorToBackpack(Desired[Slot]);
		Desired[Slot]->ForceNetUpdate();
	}
	C->RefreshFirstPersonWorldWeapons();
	if (Desired[Held])
	{
		C->GetCharacterMovement()->bOrientRotationToMovement = false;
		C->bUseControllerRotationYaw = true;
	}
	for (auto* W : Old) if (IsValid(W) && W != Desired[0] && W != Desired[1]) W->Destroy();
	C->ForceNetUpdate();
	SyncReserve();
	return true;
}

// 结算或掉落前把当前枪械弹匣和槽位写回物品实例；有变化才增加版本。
void UExtractionInventoryComponent::CaptureWeapons()
{
	auto* C = Character();
	if (!C || !C->HasAuthority()) return;
	auto* Combat = C->GetCombat().Get();
	AWeapon* Weapons[2] = {Combat->EquippedWeapon, Combat->SecondaryWeapon};
	auto Candidate = Contents;
	// 槽位固定不随切枪交换，只写回弹匣；切枪结果记在手持槽 bHoldingSecondary 上。
	for (auto* W : Weapons)
		if (W)
			if (auto* I = Candidate.Find(W->ExtractionItemId))
			{
				I->Magazine = W->GetAmmo();
			}
	if (const auto* W = Combat->EquippedWeapon)
		if (const auto* I = Contents.Find(W->ExtractionItemId); I && (I->Slot == 0 || I->Slot == 1)
			&& bHoldingSecondary != (I->Slot == 1))
		{
			bHoldingSecondary = I->Slot == 1;
			SyncReserve();
			GetOwner()->ForceNetUpdate();
		}
	CommitCandidate(MoveTemp(Candidate), false);
}

// 服务器接受射击后记录弹匣余量，并从弹匣来源中消耗一发，保持数量与来源守恒。
void UExtractionInventoryComponent::RecordShot(AWeapon* Weapon)
{
	if (!GetOwner()->HasAuthority() || !Weapon) return;
	auto Candidate = Contents;
	if (auto* I = Candidate.Find(Weapon->ExtractionItemId))
	{
		I->Magazine = Weapon->GetAmmo();
		ExtractionItems::TakeOrigins(I->MagazineOrigins, 1);
		if (CommitCandidate(MoveTemp(Candidate), false).Ok())
			Weapon->ExtractionInitialAmmo = Weapon->GetAmmo();
	}
}

// 按弹匣空位和同弹种备弹决定装填数量；来源批次随子弹从背包转入弹匣，返回实际装入发数。
int32 UExtractionInventoryComponent::Reload(AWeapon* Weapon)
{
	if (!GetOwner()->HasAuthority() || !Weapon || !Contents.Find(Weapon->ExtractionItemId)) return 0;
	const FName AmmoType = Contents.Find(Weapon->ExtractionItemId)->MagazineAmmo;
	const int32 Amount = FMath::Min(FMath::Max(0, Weapon->GetMagCapacity() - Weapon->GetAmmo()), Contents.Count(AmmoType));
	if (Amount <= 0) return 0;
	auto Candidate = Contents;
	int32 Remaining = Amount;
	auto* GunItem = Candidate.Find(Weapon->ExtractionItemId);
	for (auto& I : Candidate.Items)
		if (I.Definition == AmmoType)
		{
			const int32 Take = FMath::Min(Remaining, I.Quantity);
			ExtractionItems::MergeOrigins(GunItem->MagazineOrigins, ExtractionItems::TakeOrigins(I.Origins, Take));
			I.Quantity -= Take;
			Remaining -= Take;
		}
	GunItem->Magazine = Weapon->GetAmmo() + Amount;
	Candidate.Items.RemoveAll([](const FExtractionItem& I) { return I.Quantity == 0; });
	if (!CommitCandidate(MoveTemp(Candidate), false).Ok()) return 0;
	Weapon->SetExtractionAmmo(Weapon->GetAmmo() + Amount);
	Weapon->ExtractionInitialAmmo = Weapon->GetAmmo();
	return Amount;
}

// 生成地面容器成功后才清空背包；bDropped 阻止死亡、离局等多个入口重复掉落。
void UExtractionInventoryComponent::DropAll(const FVector& Position)
{
	if (!GetOwner()->HasAuthority() || bDropped) return;
	CaptureWeapons();
	// 安全箱及其中物品不掉落：结算时已随玩家回到仓库，这里只丢其余装备与物资。
	FExtractionContainer Dropped = Contents;
	Dropped.Items.RemoveAll([](const FExtractionItem& I) { return ExtractionItems::Protected(I); });
	AExtractionLootActor* Loot = nullptr;
	if (!Dropped.Items.IsEmpty())
	{
		Loot = AExtractionLootActor::SpawnContainer(GetWorld(), Position, false);
		if (!Loot)
		{
			UE_LOG(LogTemp, Error, TEXT("EXTRACTION_DROP_FAILED Owner=%s"), *GetOwner()->GetName());
			return;
		}
		Dropped.Id = Loot->Contents.Id;
		Dropped.Revision = Loot->Contents.Revision;
	}
	auto Candidate = Contents;
	Candidate.Items.Reset();
	const auto Result = CommitCandidate(MoveTemp(Candidate), true, Loot, Loot ? &Dropped : nullptr);
	if (!Result.Ok())
	{
		if (Loot) Loot->Destroy();
		UE_LOG(LogTemp, Error, TEXT("EXTRACTION_DROP_COMMIT_FAILED %s"), *Result.Error);
		return;
	}
	bDropped = true;
}
