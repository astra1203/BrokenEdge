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
// 局内物品的权威容器：玩家挂在 PlayerState 上，守卫可挂在角色上。
// Contents 保存物品实例，武器 Actor 是其场景表现；操作时要同步两者，不能只改变可见枪械。
// 背包内容只复制给拥有者，来源批次用于服务器的消耗、转移和崩溃恢复。

// 启用组件复制；具体哪些字段复制及发给谁由 GetLifetimeReplicatedProps 指定。
UExtractionInventoryComponent::UExtractionInventoryComponent() { SetIsReplicatedByDefault(true); }

// 背包 Contents 仅复制给所属客户端，避免把玩家完整库存广播给所有人。
void UExtractionInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UExtractionInventoryComponent, Contents, COND_OwnerOnly);
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
	bInitialized = true;
	InitializedWorld = GetWorld();
	bDropped = false;
	Contents = Member->Original;
	Contents.Id = FGuid::NewGuid();
	// 补给生成是独立于原出战装备的局内规则；容器装不下时保留为地面物资。
	const FExtractionContainer Overflow = GrantStartingSupply(Contents);
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
			const FVector Candidate = C->GetActorLocation() + C->GetActorRightVector() * Side * 100;
			FHitResult Ground, Wall;
			if (!GetWorld()->LineTraceSingleByChannel(Wall, C->GetActorLocation(), Candidate, ECC_Visibility, Query)
				&& GetWorld()->LineTraceSingleByChannel(Ground, Candidate, Candidate - FVector(0, 0, 200),
				                                        ECC_Visibility, Query)
				&& Ground.ImpactNormal.Z > .7f)
			{
				Position = Ground.ImpactPoint + FVector(0, 0, 20);
				break;
			}
		}
		if (auto* Supply = AExtractionLootActor::SpawnContainer(GetWorld(), Position, false))
		{
			Supply->Label = TEXT("开局补给");
			Supply->SetContents(Overflow);
		}
		else UE_LOG(LogTemp, Error, TEXT("EXTRACTION_STARTING_SUPPLY_SPAWN_FAILED"));
	}
	SyncWeapons();
	Changed();
}

// 测试用开局配枪：主武器 M4A1、副武器 SCAR-H，各补 60 发默认弹种。
// 槽位上原有的其他枪挪进背包网格，装不下的枪和弹药返回为脚边补给容器，不会丢失。
FExtractionContainer UExtractionInventoryComponent::GrantStartingSupply(FExtractionContainer& Inventory)
{
	FExtractionContainer Overflow;
	Overflow.Id = FGuid::NewGuid();
	const TPair<int32, FName> Guns[] = {{0, TEXT("M4A1")}, {1, TEXT("SCARH")}};
	for (const auto& [Slot, Gun] : Guns)
	{
		const auto* Def = UExtractionItemDefinition::Get(Gun);
		if (!Def) continue;
		if (const auto* Current = Inventory.InSlot(Slot); Current && Current->Definition != Gun)
		{
			FExtractionItem Moved = *Current;
			Inventory.Items.RemoveAll([&](const FExtractionItem& I) { return I.Id == Moved.Id; });
			if (!ExtractionItems::AutoPlace(Inventory, Moved)) ExtractionItems::AutoPlace(Overflow, Moved);
		}
		if (!Inventory.InSlot(Slot))
		{
			auto Item = ExtractionItems::Make(Gun);
			Item.Slot = Slot;
			Inventory.Items.Add(Item);
		}
		// Fill partial stacks even when no free grid cell can hold the whole grant.
		int32 Added = 0;
		for (; Added < 60; ++Added)
			if (!ExtractionItems::AutoPlace(Inventory, ExtractionItems::Make(Def->DefaultAmmo))) break;
		if (Added < 60) ExtractionItems::AutoPlace(Overflow, ExtractionItems::Make(Def->DefaultAmmo, 60 - Added));
	}
	++Inventory.Revision;
	return Overflow;
}

// 为服务器守卫创建独立 AK、弹药和医疗包，不读取玩家长期档案。
void UExtractionInventoryComponent::InitializeGuard()
{
	auto* C = Character();
	if (!C || !C->HasAuthority() || !C->bExtractionGuard || bInitialized) return;
	bInitialized = true;
	Contents.Id = FGuid::NewGuid();
	Contents.WeaponSlots = true;
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
	Contents.Items.Add(Gun);
	ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(GunDef ? GunDef->DefaultAmmo : FName(TEXT("Ammo")), 60));
	ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(TEXT("Medkit")));
	SyncWeapons();
	Changed();
}

// 结算后的清理路径：标记已处理并清空物品和武器，防止角色销毁时再次掉落已入库资产。
void UExtractionInventoryComponent::ClearSettled()
{
	if (!GetOwner()->HasAuthority()) return;
	bDropped = true;
	Contents.Items.Reset();
	++Contents.Revision;
	SyncWeapons();
	Changed();
}

// 从背包当前弹种总量刷新旧战斗组件的备弹显示，备弹不再是可独立增减的另一份库存。
void UExtractionInventoryComponent::SyncReserve()
{
	if (auto* C = Character())
	{
		auto* Combat = C->GetCombat().Get();
		const auto* Gun = Contents.InSlot(0);
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
	Combat->EquippedWeapon = Desired[0];
	Combat->SecondaryWeapon = Desired[1];
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
		Desired[Slot]->SetWeaponState(Slot == 0 ? EWeaponState::EWS_Equipped : EWeaponState::EWS_EquippedSecondary);
		if (Slot == 0) Combat->AttachActorToRightHand(Desired[Slot]);
		else Combat->AttachActorToBackpack(Desired[Slot]);
		Desired[Slot]->ForceNetUpdate();
	}
	C->RefreshFirstPersonWorldWeapons();
	if (Desired[0])
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
	bool ChangedState = false;
	for (int32 Slot = 0; Slot < 2; ++Slot)
		if (auto* W = Weapons[Slot])
			if (auto* I = Contents.Find(W->ExtractionItemId))
			{
				ChangedState |= I->Slot != Slot || I->Magazine != W->GetAmmo();
				I->Slot = Slot;
				I->Magazine = W->GetAmmo();
			}
	if (ChangedState)
	{
		++Contents.Revision;
		Changed();
	}
}

// 服务器接受射击后记录弹匣余量，并从弹匣来源中消耗一发，保持数量与来源守恒。
void UExtractionInventoryComponent::RecordShot(AWeapon* Weapon)
{
	if (!GetOwner()->HasAuthority() || !Weapon) return;
	if (auto* I = Contents.Find(Weapon->ExtractionItemId))
	{
		I->Magazine = Weapon->GetAmmo();
		ExtractionItems::TakeOrigins(I->MagazineOrigins, 1);
		Weapon->ExtractionInitialAmmo = Weapon->GetAmmo();
		++Contents.Revision;
		GetOwner()->ForceNetUpdate();
	}
}

// 按弹匣空位和同弹种备弹决定装填数量；来源批次随子弹从背包转入弹匣，返回实际装入发数。
int32 UExtractionInventoryComponent::Reload(AWeapon* Weapon)
{
	if (!GetOwner()->HasAuthority() || !Weapon || !Contents.Find(Weapon->ExtractionItemId)) return 0;
	const FName AmmoType = Contents.Find(Weapon->ExtractionItemId)->MagazineAmmo;
	int32 Amount = FMath::Min(Weapon->GetMagCapacity() - Weapon->GetAmmo(), Contents.Count(AmmoType));
	int32 Remaining = Amount;
	auto* GunItem = Contents.Find(Weapon->ExtractionItemId);
	for (auto& I : Contents.Items)
		if (I.Definition == AmmoType)
		{
			const int32 Take = FMath::Min(Remaining, I.Quantity);
			ExtractionItems::MergeOrigins(GunItem->MagazineOrigins, ExtractionItems::TakeOrigins(I.Origins, Take));
			I.Quantity -= Take;
			Remaining -= Take;
		}
	Contents.Items.RemoveAll([](const FExtractionItem& I) { return I.Quantity == 0; });
	Weapon->SetExtractionAmmo(Weapon->GetAmmo() + Amount);
	Weapon->ExtractionInitialAmmo = Weapon->GetAmmo();
	Contents.Find(Weapon->ExtractionItemId)->Magazine = Weapon->GetAmmo();
	++Contents.Revision;
	Changed();
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
	if (!Dropped.Items.IsEmpty())
	{
		auto* Loot = AExtractionLootActor::SpawnContainer(GetWorld(), Position, false);
		if (!Loot)
		{
			UE_LOG(LogTemp, Error, TEXT("EXTRACTION_DROP_FAILED Owner=%s"), *GetOwner()->GetName());
			return;
		}
		Dropped.Id = FGuid::NewGuid();
		++Dropped.Revision;
		Loot->SetContents(Dropped);
	}
	bDropped = true;
	Contents.Items.Reset();
	++Contents.Revision;
	SyncWeapons();
	Changed();
}
