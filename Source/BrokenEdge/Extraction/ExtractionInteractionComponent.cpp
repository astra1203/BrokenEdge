#include "ExtractionInteractionComponent.h"
#include "ExtractionDoor.h"
#include "ExtractionHealth.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionLootActor.h"
#include "ExtractionKeyRoom.h"
#include "ExtractionExitSwitch.h"
#include "ExtractionZipline.h"
#include "ExtractionState.h"
#include "ExtractionAbilities.h"
#include "ExtractionCombatComponent.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
// 交互把本地选中目标与服务器物品事务分开：焦点用于提示，真正操作仍需服务器重新检查距离、遮挡和版本。
// 搜索结果按玩家独立保存，LootView 是已脱敏的容器视图，不直接广播服务器完整 Contents。

UExtractionInteractionComponent::UExtractionInteractionComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = .1f;
}

void UExtractionInteractionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UExtractionInteractionComponent, InventoryOpen, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UExtractionInteractionComponent, Session, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UExtractionInteractionComponent, Loot, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UExtractionInteractionComponent, LootView, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UExtractionInteractionComponent, SearchEnd, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UExtractionInteractionComponent, MedicalEnd, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UExtractionInteractionComponent, MedicalDuration, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UExtractionInteractionComponent, MedicalItem, COND_OwnerOnly);
}

// 获取所属角色；玩家与守卫库存的选择集中在 Inventory 中。
ABrokenEdgeCharacter* UExtractionInteractionComponent::Character() const
{
	return Cast<ABrokenEdgeCharacter>(GetOwner());
}

// 优先读取玩家 PlayerState 背包；无玩家状态的守卫使用 GuardInventory。
UExtractionInventoryComponent* UExtractionInteractionComponent::Inventory() const
{
	auto* PS = Character() ? Character()->GetPlayerState<AExtractionPlayerState>() : nullptr;
	return PS
		       ? PS->Inventory.Get()
		       : Character() && Character()->bExtractionGuard
		       ? Character()->GuardInventory.Get()
		       : nullptr;
}

bool UExtractionInteractionComponent::IsEnabled() const
{
	return GetWorld() && GetWorld()->GetGameState<AExtractionGameState>();
}

bool UExtractionInteractionComponent::CanAct() const
{
	return Character() && Character()->GetExtractionCombat()->CanAct() && Inventory() && !Inventory()->WasDropped();
}

// 检查门锁、200 厘米距离与视线；只忽略自身和手持武器，目标本身命中不算遮挡。
bool UExtractionInteractionComponent::CanReach(const AExtractionLootActor* Target) const
{
	if (IsValid(Target) && Target->AccessDoor && !Target->AccessDoor->Open) return false;
	auto* C = Character();
	if (!IsValid(Target) || !C || FVector::DistSquared(C->GetActorLocation(), Target->InteractionPoint()) >
		FMath::Square(200.f))
		return false;
	FVector Eye;
	FRotator Rotation;
	C->GetActorEyesViewPoint(Eye, Rotation);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ExtractionInteraction), false, C);
	if (C->GetEquippedWeapon()) Params.AddIgnoredActor(C->GetEquippedWeapon());
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, Eye, Target->InteractionPoint(), ECC_Visibility, Params)
		|| Hit.GetActor() == Target;
}

// 本地按距离与朝向评分挑选战利品或钥匙门，两种焦点互斥；界面打开时不重新选取。
void UExtractionInteractionComponent::UpdateFocus()
{
	Focus = nullptr;
	OrdinaryDoorFocus = nullptr;
	DoorFocus = nullptr;
	SwitchFocus = nullptr;
	ZiplineFocus = nullptr;
	if (!CanAct() || InventoryOpen) return;
	auto* C = Character();
	const FVector Forward = C->GetControlRotation().Vector().GetSafeNormal2D();
	float Best = TNumericLimits<float>::Max();
	for (TActorIterator<AExtractionLootActor> It(GetWorld()); It; ++It)
	{
		if ((It->Loose && It->Empty) || !CanReach(*It)) continue;
		const FVector Delta = It->InteractionPoint() - C->GetActorLocation();
		const float Dot = FVector::DotProduct(Forward, Delta.GetSafeNormal2D());
		if (Dot < -.1f) continue;
		const float Score = Delta.Size() + (1 - Dot) * 100;
		if (Score < Best)
		{
			Focus = *It;
			Best = Score;
		}
	}
	for (TActorIterator<AExtractionDoor> It(GetWorld()); It; ++It)
	{
		if (!It->CanReach(C)) continue;
		const FVector Delta = It->InteractionPoint() - C->GetActorLocation();
		const float Dot = FVector::DotProduct(Forward, Delta.GetSafeNormal2D());
		const float Score = Delta.Size() + (1 - Dot) * 100;
		if (Dot >= -.1f && Score < Best) { OrdinaryDoorFocus = *It; Focus = nullptr; Best = Score; }
	}
	for (TActorIterator<AExtractionKeyRoom> It(GetWorld()); It; ++It)
	{
		// 距离由门自己的交互碰撞盒决定；站进盒子就算够得着，只要求不是背对门。
		if (It->Unlocked || !It->CanReach(C)) continue;
		const FVector Delta = It->GetActorLocation() - C->GetActorLocation();
		const float Dot = FVector::DotProduct(Forward, Delta.GetSafeNormal2D());
		const float Score = Delta.Size();
		if (Dot >= -.5f && Score < Best)
		{
			OrdinaryDoorFocus = nullptr;
			DoorFocus = *It;
			Focus = nullptr;
			Best = Score;
		}
	}
	for (TActorIterator<AExtractionExitSwitch> It(GetWorld()); It; ++It)
	{
		if (!It->CanReach(C)) continue;
		const FVector Delta = It->GetActorLocation() - C->GetActorLocation();
		const float Dot = FVector::DotProduct(Forward, Delta.GetSafeNormal2D());
		const float Score = Delta.Size2D();
		if (Dot >= -.5f && Score < Best)
		{
			OrdinaryDoorFocus = nullptr;
			SwitchFocus = *It;
			DoorFocus = nullptr;
			Focus = nullptr;
			Best = Score;
		}
	}
	// 滑索端点：站在立柱旁就优先挂索。
	for (TActorIterator<AExtractionZipline> It(GetWorld()); It; ++It)
	{
		bool bFromTop = false;
		if (!It->CanReach(C, bFromTop)) continue;
		const float Score = FVector::Dist2D(C->GetActorLocation(), bFromTop ? It->TopWorld() : It->BottomWorld()) - 50.f;
		if (Score < Best)
		{
			OrdinaryDoorFocus = nullptr;
			ZiplineFocus = *It;
			SwitchFocus = nullptr;
			DoorFocus = nullptr;
			Focus = nullptr;
			Best = Score;
		}
	}
}

// 服务器只接受更新的开关会话号，先取消旧交互；散落物直接拾取，容器则进入搜索界面。
// 拾取先在副本上转移，再同步武器，生成失败恢复原背包，不能让物资消失。
void UExtractionInteractionComponent::Open(uint32 NewSession, AExtractionLootActor* Target, bool bOpen)
{
	if (!GetOwner()->HasAuthority() || NewSession <= Session) return;
	Session = NewSession;
	CancelMedical();
	Close();
	if (!bOpen || !CanAct()) return;
	if (Target && !CanReach(Target)) return;
	auto* C = Character();
	C->GetExtractionCombat()->CancelReload();
	C->GetCombat()->FireButtonPressed(false);
	C->GetCombat()->SetAiming(false);
	if (Target && Target->Loose)
	{
		if (Target->GetContents().Items.IsEmpty()) return;
		if (C->GetCombatState() != ECombatState::ECS_Unoccuiped) return;
		// 整包落地的胸挂/背包：对应装备槽空着就连同里面的东西整包穿上；
		// 槽位已占用且包里还有东西时打开搜索界面逐件拿（包里的东西直接可见，不用再搜）。
		if (Target->GetContents().GearGrids)
		{
			const auto Result = UExtractionInventoryComponent::MutatePair(*Inventory(), *Target,
				[](FExtractionContainer& Destination, FExtractionContainer& Bundle, FString& Error)
				{ return ExtractionItems::AttachGear(Destination, Bundle, Error); });
			if (Result.Ok())
			{
				Message.Reset();
				return;
			}
			if (Result.Code == EExtractionInventoryCommit::WeaponSyncFailed)
			{
				Message = TEXT("武器生成失败，操作已撤回");
				return;
			}
			if (Target->GetContents().Items.Num() > 1)
			{
				auto& Known = Revealed.FindOrAdd(Target->GetContents().Id);
				for (const auto& I : Target->GetContents().Items) Known.Add(I.Id);
				InventoryOpen = true;
				Loot = Target;
				SeenRevision = -1;
				RefreshLootView();
				GetOwner()->ForceNetUpdate();
				return;
			}
		}
		const auto& Item = Target->GetContents().Items[0];
		FExtractionItemRequest R;
		R.Item = Item.Id;
		R.Quantity = Item.Quantity;
		const auto* PickDef = UExtractionItemDefinition::Get(Item.Definition);
		if (PickDef && PickDef->Kind == EExtractionItemKind::Weapon)
			for (int32 Slot = 0; Slot < 2; ++Slot)
				if (!Inventory()->GetContents().InSlot(Slot))
				{
					R.Slot = Slot;
					break;
				}
		// 捡装备先看装备槽：没穿胸挂/背包/安全箱时直接穿上，不需要网格里有空位。
		if (PickDef && PickDef->Kind == EExtractionItemKind::Gear && PickDef->GearSlot >= ExtractionGear::RigSlot
			&& PickDef->GearSlot <= ExtractionGear::SafeSlot && !Inventory()->GetContents().InSlot(PickDef->GearSlot))
			R.Slot = PickDef->GearSlot;
		const auto Result = UExtractionInventoryComponent::MutatePair(*Inventory(), *Target,
			[&](FExtractionContainer& Destination, FExtractionContainer& Source, FString& Error)
			{ return ExtractionItems::Transfer(Source, Destination, R, Error); });
		Message = Result.Ok() ? FString() : Result.Code == EExtractionInventoryCommit::WeaponSyncFailed
			? TEXT("武器生成失败，操作已撤回") : Result.Error;
		return;
	}
	InventoryOpen = true;
	Loot = Target;
	if (Target) Target->MarkOpened(); // Tab 打开自己背包时没有容器
	SeenRevision = -1;
	RefreshLootView();
	GetOwner()->ForceNetUpdate();
}

// 清空当前打开容器、搜索计时和临时视图；保留玩家已经揭示过的物品记忆。
void UExtractionInteractionComponent::Close()
{
	InventoryOpen = false;
	Loot = nullptr;
	LootView = FExtractionContainer();
	SearchEnd = 0;
	Searching.Invalidate();
	SeenRevision = -1;
	if (GetOwner()->HasAuthority()) GetOwner()->ForceNetUpdate();
}

// 按容器 ID 和物品 ID 查询本玩家搜索记录；其他玩家搜过不等于本人已经揭示。
bool UExtractionInteractionComponent::IsRevealed(const FGuid& Id) const
{
	if (!Loot) return false;
	const auto* Known = Revealed.Find(Loot->GetContents().Id);
	return Known && Known->Contains(Id);
}

// 复制服务器容器后擦除未揭示条目的身份、类型和装备信息，仅给所属客户端发送脱敏快照。
void UExtractionInteractionComponent::RefreshLootView()
{
	if (!GetOwner()->HasAuthority() || !Loot) return;
	LootView = Loot->GetContents();
	for (auto& I : LootView.Items)
		if (!IsRevealed(I.Id))
		{
			I.Id.Invalidate();
			I.Definition = NAME_None;
			I.Quantity = 0;
			I.Magazine = 0;
			I.MagazineAmmo = NAME_None;
			I.Durability = 0;
			I.Attachments.Reset();
		}
	SeenRevision = Loot->GetContents().Revision;
	GetOwner()->ForceNetUpdate();
}

// 按槽位/网格顺序每秒揭示一件；目标被他人取走时取消该条搜索，并用容器版本刷新视图。
void UExtractionInteractionComponent::AdvanceSearch(double Now)
{
	if (!Loot || !InventoryOpen) return;
	if (Searching.IsValid() && !Loot->GetContents().Find(Searching))
	{
		Searching.Invalidate();
		SearchEnd = 0;
	}
	if (Searching.IsValid() && Now >= SearchEnd)
	{
		Revealed.FindOrAdd(Loot->GetContents().Id).Add(Searching);
		Searching.Invalidate();
		SearchEnd = 0;
		RefreshLootView();
	}
	if (!Searching.IsValid())
	{
		TArray<FExtractionItem> Ordered = Loot->GetContents().Items;
		Ordered.Sort([](const FExtractionItem& A, const FExtractionItem& B)
		{
			// 装备槽在前，网格按分区（胸挂 -2、背包 -1 …）分段，再按格子行列。
			auto Key = [](const FExtractionItem& I)
			{
				return I.Slot >= 0 ? I.Slot - 100 : (I.Slot == -2 ? 0 : -I.Slot * 1000) + I.Cell.Y * 16 + I.Cell.X;
			};
			const int32 KA = Key(A);
			const int32 KB = Key(B);
			return KA < KB;
		});
		for (const auto& I : Ordered)
			if (!IsRevealed(I.Id))
			{
				Searching = I.Id;
				SearchEnd = Now + 1;
				break;
			}
	}
	if (SeenRevision != Loot->GetContents().Revision) RefreshLootView();
}

// 服务器清除医疗计时和物品占用，并取消能力动作；未完成时不消耗医疗物品。
void UExtractionInteractionComponent::CancelMedical()
{
	if (!GetOwner()->HasAuthority()) return;
	Character()->GetExtractionAbilities()->Cancel();
	MedicalEnd = 0;
	MedicalItem.Invalidate();
	// 能力组件取消时也会停表现，重复调用安全。
	Character()->StopAbilityMontage();
	GetOwner()->ForceNetUpdate();
}

// 医疗物品的开始/结束都通过 MedicalEnd 复制给拥有者，客户端据此播放或停止治疗表现。
void UExtractionInteractionComponent::OnRep_MedicalEnd()
{
	auto* C = Character();
	if (C == nullptr) return;
	if (MedicalEnd > 0)
		C->PlayAbilityMontage(static_cast<uint8>(EExtractionAction::Heal), static_cast<float>(MedicalDuration));
	else C->StopAbilityMontage();
}

// 停止持续射击并退出瞄准，供交互、能力和失能路径共用。
void UExtractionInteractionComponent::StopCombatInput()
{
	if (auto* C = Character())
	{
		C->GetCombat()->FireButtonPressed(false);
		C->GetCombat()->SetAiming(false);
	}
}

// 验证物品类型、动作互斥和治疗需求，再记录服务器完成时刻；空错误字符串表示启动成功。
FString UExtractionInteractionComponent::StartMedical(const FGuid& Id)
{
	const auto* I = Inventory()->GetContents().Find(Id);
	const auto* D = I ? UExtractionItemDefinition::Get(I->Definition) : nullptr;
	auto* C = Character();
	if (!D || D->Kind != EExtractionItemKind::Medical) return TEXT("无法使用该物品");
	if (C->GetExtractionAbilities()->Action != EExtractionAction::None || MedicalEnd > 0 || C->GetCombatState() !=
		ECombatState::ECS_Unoccuiped)
		return TEXT("当前动作尚未完成");
	if (!C->GetExtractionHealth()->Needs(D->MedicalEffect)) return TEXT("当前无需使用该医疗物品");
	Close();
	C->GetCombat()->FireButtonPressed(false);
	C->GetCombat()->SetAiming(false);
	MedicalItem = Id;
	MedicalDuration = FMath::Max(.1, static_cast<double>(D->UseSeconds));
	MedicalEnd = GetWorld()->GetTimeSeconds() + MedicalDuration;
	// 服务器侧先挂表现（监听服务器可见），远程客户端的拥有者由 OnRep_MedicalEnd 触发。
	C->PlayAbilityMontage(static_cast<uint8>(EExtractionAction::Heal), static_cast<float>(MedicalDuration));
	GetOwner()->ForceNetUpdate();
	return {};
}

// 从角色前方寻找无墙阻挡且有地面的放置点，忽略玩家、武器和战利品以避免落在其顶上。
FVector UExtractionInteractionComponent::DropPosition() const
{
	const auto* C = Character();
	FVector Point = C->GetActorLocation() + C->GetActorForwardVector() * 70;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ExtractionDrop), false, C);
	for (TActorIterator<APawn> It(GetWorld()); It; ++It) Params.AddIgnoredActor(*It);
	for (TActorIterator<AWeapon> It(GetWorld()); It; ++It) Params.AddIgnoredActor(*It);
	for (TActorIterator<AExtractionLootActor> It(GetWorld()); It; ++It) Params.AddIgnoredActor(*It);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, C->GetActorLocation(), Point, ECC_Visibility, Params))
		Point = C->GetActorLocation();
	if (GetWorld()->LineTraceSingleByChannel(Hit, Point, Point - FVector(0, 0, 250), ECC_Visibility, Params))
		Point = Hit.ImpactPoint + FVector(0, 0, 20);
	return Point;
}

// 全部拾取：把容器里已揭示的物品逐件自动放进携行网格（胸挂优先、再背包，不进安全箱），放不下的留在原处。
FString UExtractionInteractionComponent::TakeAll(const FExtractionItemRequest& R)
{
	auto* Inv = Inventory();
	if (!InventoryOpen || R.Session != Session || !Loot || !CanReach(Loot)) return TEXT("搜索已关闭或超出距离");
	if (R.LootRevision != Loot->GetContents().Revision)
	{
		RefreshLootView();
		return TEXT("容器已变化，请重试");
	}
	if (MedicalEnd > 0 || Character()->GetExtractionAbilities()->Action != EExtractionAction::None)
		return TEXT("当前动作尚未完成");
	int32 Moved = 0, Left = 0;
	const auto Result = UExtractionInventoryComponent::MutatePair(*Inv, *Loot,
		[&](FExtractionContainer& Candidate, FExtractionContainer& Other, FString& Error)
		{
			const auto Items = Other.Items; // Transfers mutate Other; iterate the original list.
			for (const auto& Item : Items)
			{
				if (!IsRevealed(Item.Id)) continue;
				FExtractionItemRequest Move;
				Move.Item = Item.Id;
				Move.FromLoot = true;
				Move.Quantity = Item.Quantity;
				Move.Slot = -1;
				Move.AutoPlace = true;
				FString ItemError;
				if (ExtractionItems::Transfer(Other, Candidate, Move, ItemError)) ++Moved;
				else ++Left;
			}
			if (Moved) return true;
			Error = Left ? TEXT("背包空间不足") : TEXT("没有可拾取的物品");
			return false;
		}, R.InventoryRevision, R.LootRevision);
	if (!Result.Ok())
		return Result.Code == EExtractionInventoryCommit::WeaponSyncFailed
			? TEXT("武器生成失败，操作已撤回") : Result.Code == EExtractionInventoryCommit::Stale
			? TEXT("背包或容器已变化，请重试") : Result.Error;
	RefreshLootView();
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_TAKE_ALL Sequence=%u Moved=%d Left=%d"), R.Sequence, Moved, Left);
	return Left ? FString::Printf(TEXT("已拾取 %d 件，%d 件放不下"), Moved, Left) : FString();
}

// 服务器事务入口：依次校验序号、背包版本、容器会话、距离和搜索权限，再执行转移/丢弃/装配。
// 保存原库存用于武器生成失败时回滚；成功后才通知容器和背包更新，返回空字符串表示成功。
FString UExtractionInteractionComponent::Execute(const FExtractionItemRequest& R)
{
	if (!GetOwner()->HasAuthority() || R.Sequence <= LastSequence) return TEXT("重复或过期请求");
	LastSequence = R.Sequence;
	if (!CanAct()) return TEXT("当前无法操作物品");
	auto* Inv = Inventory();
	if (R.InventoryRevision != Inv->GetContents().Revision) return TEXT("背包已变化，请重试");
	if (R.Op == EExtractionItemOp::TakeAll) return TakeAll(R);
	if (R.FromLoot || R.ToLoot)
	{
		if (!InventoryOpen || R.Session != Session || !Loot || !CanReach(Loot)) return TEXT("搜索已关闭或超出距离");
		if (R.LootRevision != Loot->GetContents().Revision)
		{
			RefreshLootView();
			return TEXT("容器已变化，请重试");
		}
		if ((R.FromLoot && !IsRevealed(R.Item)) || (R.ToLoot && R.MergeInto.IsValid() && !IsRevealed(R.MergeInto))
			|| (R.ToLoot && R.Op == EExtractionItemOp::Swap && !IsRevealed(R.SwapWith)))
			return TEXT("物品尚未搜索");
	}
	// 整理不针对单件物品：只重排自己携行网格里的物品，装备槽不动，医疗中不整理以免挪走正在用的物品。
	if (R.Op == EExtractionItemOp::Sort)
	{
		if (R.FromLoot || R.ToLoot) return TEXT("只能整理自己的背包");
		if (MedicalEnd > 0 || MedicalItem.IsValid()) return TEXT("当前动作尚未完成");
		const auto Result = Inv->Mutate([](FExtractionContainer& Candidate, FString& Error)
		{
			if (ExtractionItems::Sort(Candidate)) return true;
			Error = TEXT("空间不足，无法整理");
			return false;
		}, R.InventoryRevision, false);
		return Result.Ok() ? FString() : Result.Error;
	}
	const FExtractionContainer& OriginalSource = R.FromLoot ? Loot->GetContents() : Inv->GetContents();
	const auto* Item = OriginalSource.Find(R.Item);
	if (!Item || R.Quantity < 1 || R.Quantity > Item->Quantity) return TEXT("物品已变化或数量无效");
	if (MedicalItem.IsValid() && (R.Item == MedicalItem || R.MergeInto == MedicalItem || R.SwapWith == MedicalItem)) return TEXT("物品正在使用");
	if (R.Op == EExtractionItemOp::Use)
		return R.FromLoot ? TEXT("请先拾取医疗包") : StartMedical(R.Item);
	auto* C = Character();
	if (MedicalEnd > 0 || C->GetExtractionAbilities()->Action != EExtractionAction::None) return TEXT("当前动作尚未完成");
	if (R.Op == EExtractionItemOp::Drop) C->GetExtractionCombat()->CancelReload();
	if (C->GetCombatState() != ECombatState::ECS_Unoccuiped) return TEXT("当前动作尚未完成");
	const FExtractionContainer BeforeLoot = Loot ? Loot->GetContents() : FExtractionContainer();
	FExtractionInventoryResult Result;
	if (R.Op == EExtractionItemOp::Attach || R.Op == EExtractionItemOp::Detach || R.Op == EExtractionItemOp::Unload
		|| R.Op == EExtractionItemOp::SelectAmmo || R.Op == EExtractionItemOp::Tune)
	{
		Result = Inv->Mutate([&](FExtractionContainer& Candidate, FString& Error)
		{ return ExtractionItems::Configure(Candidate, R, Error); }, R.InventoryRevision);
	}
	else if (R.Op == EExtractionItemOp::Drop)
	{
		if (R.FromLoot || R.ToLoot) return TEXT("请先将物品放入背包");
		if (ExtractionItems::IsSafeBox(*Item)) return TEXT("安全箱不能丢弃");
		const FName Definition = Item->Definition;
		FExtractionContainer Candidate = Inv->GetContents(), Bundle;
		FString Error;
		if (Item->Slot == ExtractionGear::RigSlot || Item->Slot == ExtractionGear::BagSlot)
		{
			if (!ExtractionItems::DetachGear(Candidate, R.Item, Bundle, Error)) return Error;
		}
		else
		{
			FExtractionItemRequest Move = R;
			Move.Slot = -1;
			Move.AutoPlace = true;
			Move.MergeInto.Invalidate();
			if (!ExtractionItems::Transfer(Candidate, Bundle, Move, Error)) return Error;
		}
		auto* Dropped = AExtractionLootActor::SpawnContainer(GetWorld(), DropPosition(), true, Definition);
		if (!Dropped) return TEXT("无法放置地面物品");
		Bundle.Id = Dropped->GetContents().Id;
		Bundle.Revision = Dropped->GetContents().Revision;
		Result = UExtractionInventoryComponent::MutatePair(*Inv, *Dropped,
			[&](FExtractionContainer& InventoryCandidate, FExtractionContainer& Other, FString&)
			{ InventoryCandidate = Candidate; Other = Bundle; return true; }, R.InventoryRevision);
		if (!Result.Ok()) Dropped->Destroy();
	}
	else if (R.Op == EExtractionItemOp::Transfer || R.Op == EExtractionItemOp::Swap)
	{
		if (R.ToLoot && ExtractionItems::IsSafeBox(*Item)) return TEXT("安全箱不能放进容器");
		const auto& Destination = R.ToLoot ? Loot->GetContents() : Inv->GetContents();
		if (R.Op == EExtractionItemOp::Swap && R.FromLoot)
			if (const auto* Other = Destination.Find(R.SwapWith); Other && ExtractionItems::IsSafeBox(*Other))
				return TEXT("安全箱不能放进容器");
		if (R.FromLoot || R.ToLoot)
			Result = UExtractionInventoryComponent::MutatePair(*Inv, *Loot,
				[&](FExtractionContainer& Candidate, FExtractionContainer& Other, FString& Error)
				{ return ExtractionItems::Transfer(R.FromLoot ? Other : Candidate, R.ToLoot ? Other : Candidate, R, Error); },
				R.InventoryRevision, R.LootRevision);
		else
			Result = Inv->Mutate([&](FExtractionContainer& Candidate, FString& Error)
			{ return ExtractionItems::Transfer(Candidate, Candidate, R, Error); }, R.InventoryRevision);
	}
	else return TEXT("无效操作");
	if (!Result.Ok()) return Result.Code == EExtractionInventoryCommit::WeaponSyncFailed
		? TEXT("武器生成失败，操作已撤回") : Result.Code == EExtractionInventoryCommit::Stale
		? TEXT("背包或容器已变化，请重试") : Result.Error;
	if (Loot)
	{
		// The depositor knows their own deposited items; other viewers still search independently.
		if (R.ToLoot || R.Op == EExtractionItemOp::Swap)
			for (const auto& I : Loot->GetContents().Items)
				if (!BeforeLoot.Find(I.Id)) Revealed.FindOrAdd(Loot->GetContents().Id).Add(I.Id);
		RefreshLootView();
	}
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_TRANSFER Sequence=%u Item=%s Quantity=%d Op=%d FromLoot=%d ToLoot=%d"),
	       R.Sequence, *R.Item.ToString(), R.Quantity, static_cast<int32>(R.Op), R.FromLoot, R.ToLoot);
	return {};
}

// 本地只更新焦点；服务器负责越界关闭、搜索和医疗计时，移动或下落会中断治疗。
void UExtractionInteractionComponent::TickComponent(float Delta, ELevelTick TickType,
                                                    FActorComponentTickFunction* Function)
{
	Super::TickComponent(Delta, TickType, Function);
	if (!IsEnabled()) return;
	if (Character()->IsLocallyControlled()) UpdateFocus();
	if (!GetOwner()->HasAuthority()) return;
	if (!CanAct())
	{
		if (InventoryOpen || Loot) Close();
		if (MedicalEnd) CancelMedical();
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Loot && !CanReach(Loot)) Close();
	AdvanceSearch(Now);
	if (MedicalEnd > 0)
	{
		auto* C = Character();
		if (C->GetCharacterMovement()->IsFalling() || C->GetVelocity().Size2D() > 5)
		{
			CancelMedical();
			return;
		}
		// 到时再次确认医疗物品仍存在且治疗有效，成功后才移除物品并增加背包版本。
		if (Now >= MedicalEnd)
		{
			auto* Inv = Inventory();
			auto* I = Inv->GetContents().Find(MedicalItem);
			const auto* D = I ? UExtractionItemDefinition::Get(I->Definition) : nullptr;
			if (D && (D->MedicalEffect != EExtractionMedicalEffect::Heal || (FMath::IsFinite(D->HealAmount) && D->HealAmount > 0))
				&& C->GetExtractionHealth()->Needs(D->MedicalEffect) && Inv->ConsumeOne(MedicalItem).Ok()
				&& C->GetExtractionHealth()->Treat(D))
			{
				UE_LOG(LogTemp, Display, TEXT("EXTRACTION_MEDICAL Item=%s Health=%.1f"), *MedicalItem.ToString(),
				       C->GetHealth());
			}
			CancelMedical();
		}
	}
}

// 服务器退出清理统一取消搜索、医疗、装填和切枪，再交给背包执行防重复的整体掉落。
void UExtractionInteractionComponent::DropOnExit()
{
	if (!GetOwner()->HasAuthority()) return;
	Close();
	CancelMedical();
	Character()->GetExtractionCombat()->CancelReload();
	Character()->GetExtractionCombat()->CancelSwap();
	if (Inventory()) Inventory()->DropAll(DropPosition());
}
