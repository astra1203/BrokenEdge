#include "ExtractionInteractionComponent.h"
#include "ExtractionHealth.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionLootActor.h"
#include "ExtractionKeyRoom.h"
#include "ExtractionState.h"
#include "ExtractionAbilities.h"
#include "ExtractionCombatComponent.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"

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

ABrokenEdgeCharacter* UExtractionInteractionComponent::Character() const
{
	return Cast<ABrokenEdgeCharacter>(GetOwner());
}

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

void UExtractionInteractionComponent::UpdateFocus()
{
	Focus = nullptr;
	DoorFocus = nullptr;
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
	for (TActorIterator<AExtractionKeyRoom> It(GetWorld()); It; ++It)
	{
		if (It->Unlocked || !It->CanReach(C)) continue;
		const FVector Delta = It->GetActorLocation() - C->GetActorLocation();
		const float Dot = FVector::DotProduct(Forward, Delta.GetSafeNormal2D());
		const float Score = Delta.Size() + (1 - Dot) * 100;
		if (Dot >= -.1f && Score < Best)
		{
			DoorFocus = *It;
			Focus = nullptr;
			Best = Score;
		}
	}
}

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
		if (Target->Contents.Items.IsEmpty()) return;
		if (C->GetCombatState() != ECombatState::ECS_Unoccuiped) return;
		const auto& Item = Target->Contents.Items[0];
		FExtractionItemRequest R;
		R.Item = Item.Id;
		R.Quantity = Item.Quantity;
		if (Item.Definition == TEXT("AK"))
			for (int32 Slot = 0; Slot < 2; ++Slot)
				if (!Inventory()->Contents.InSlot(Slot))
				{
					R.Slot = Slot;
					break;
				}
		FExtractionContainer Source = Target->Contents, Destination = Inventory()->Contents;
		FString Error;
		if (ExtractionItems::Transfer(Source, Destination, R, Error))
		{
			const auto Before = Inventory()->Contents;
			Inventory()->Contents = Destination;
			if (!Inventory()->SyncWeapons())
			{
				Inventory()->Contents = Before;
				Error = TEXT("武器生成失败");
			}
			else
			{
				Target->SetContents(Source);
				Inventory()->Changed();
			}
		}
		Message = Error;
		return;
	}
	InventoryOpen = true;
	Loot = Target;
	SeenRevision = -1;
	RefreshLootView();
	GetOwner()->ForceNetUpdate();
}

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

bool UExtractionInteractionComponent::IsRevealed(const FGuid& Id) const
{
	if (!Loot) return false;
	const auto* Known = Revealed.Find(Loot->Contents.Id);
	return Known && Known->Contains(Id);
}

void UExtractionInteractionComponent::RefreshLootView()
{
	if (!GetOwner()->HasAuthority() || !Loot) return;
	LootView = Loot->Contents;
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
	SeenRevision = Loot->Contents.Revision;
	GetOwner()->ForceNetUpdate();
}

void UExtractionInteractionComponent::AdvanceSearch(double Now)
{
	if (!Loot || !InventoryOpen) return;
	if (Searching.IsValid() && !Loot->Contents.Find(Searching))
	{
		Searching.Invalidate();
		SearchEnd = 0;
	}
	if (Searching.IsValid() && Now >= SearchEnd)
	{
		Revealed.FindOrAdd(Loot->Contents.Id).Add(Searching);
		Searching.Invalidate();
		SearchEnd = 0;
		RefreshLootView();
	}
	if (!Searching.IsValid())
	{
		TArray<FExtractionItem> Ordered = Loot->Contents.Items;
		Ordered.Sort([](const FExtractionItem& A, const FExtractionItem& B)
		{
			const int32 KA = A.Slot >= 0 ? A.Slot - 2 : A.Cell.Y * 16 + A.Cell.X;
			const int32 KB = B.Slot >= 0 ? B.Slot - 2 : B.Cell.Y * 16 + B.Cell.X;
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
	if (SeenRevision != Loot->Contents.Revision) RefreshLootView();
}

void UExtractionInteractionComponent::CancelMedical()
{
	if (!GetOwner()->HasAuthority()) return;
	Character()->GetExtractionAbilities()->Cancel();
	MedicalEnd = 0;
	MedicalItem.Invalidate();
	GetOwner()->ForceNetUpdate();
}

void UExtractionInteractionComponent::StopCombatInput()
{
	if (auto* C = Character())
	{
		C->GetCombat()->FireButtonPressed(false);
		C->GetCombat()->SetAiming(false);
	}
}

FString UExtractionInteractionComponent::StartMedical(const FGuid& Id)
{
	const auto* I = Inventory()->Contents.Find(Id);
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
	GetOwner()->ForceNetUpdate();
	return {};
}

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

FString UExtractionInteractionComponent::Execute(const FExtractionItemRequest& R)
{
	if (!GetOwner()->HasAuthority() || R.Sequence <= LastSequence) return TEXT("重复或过期请求");
	LastSequence = R.Sequence;
	if (!CanAct()) return TEXT("当前无法操作物品");
	auto* Inv = Inventory();
	if (R.InventoryRevision != Inv->Contents.Revision) return TEXT("背包已变化，请重试");
	if (R.FromLoot || R.ToLoot)
	{
		if (!InventoryOpen || R.Session != Session || !Loot || !CanReach(Loot)) return TEXT("搜索已关闭或超出距离");
		if (R.LootRevision != Loot->Contents.Revision)
		{
			RefreshLootView();
			return TEXT("容器已变化，请重试");
		}
		if ((R.FromLoot && !IsRevealed(R.Item)) || (R.ToLoot && R.MergeInto.IsValid() && !IsRevealed(R.MergeInto)))
			return TEXT("物品尚未搜索");
	}
	FExtractionContainer& OriginalSource = R.FromLoot ? Loot->Contents : Inv->Contents;
	const auto* Item = OriginalSource.Find(R.Item);
	if (!Item || R.Quantity < 1 || R.Quantity > Item->Quantity) return TEXT("物品已变化或数量无效");
	if (R.Item == MedicalItem || R.MergeInto == MedicalItem && MedicalItem.IsValid()) return TEXT("物品正在使用");
	if (R.Op == EExtractionItemOp::Use)
		return R.FromLoot ? TEXT("请先拾取医疗包") : StartMedical(R.Item);
	auto* C = Character();
	if (MedicalEnd > 0 || C->GetExtractionAbilities()->Action != EExtractionAction::None) return TEXT("当前动作尚未完成");
	if (R.Op == EExtractionItemOp::Drop) C->GetExtractionCombat()->CancelReload();
	if (C->GetCombatState() != ECombatState::ECS_Unoccuiped) return TEXT("当前动作尚未完成");
	FExtractionContainer BeforeInventory = Inv->Contents;
	FExtractionContainer BeforeLoot = Loot ? Loot->Contents : FExtractionContainer();
	FString Error;
	AExtractionLootActor* Dropped = nullptr;
	if (R.Op == EExtractionItemOp::Attach || R.Op == EExtractionItemOp::Detach || R.Op == EExtractionItemOp::Unload || R
		.Op == EExtractionItemOp::SelectAmmo || R.Op == EExtractionItemOp::Tune)
	{
		if (!ExtractionItems::Configure(Inv->Contents, R, Error)) return Error;
	}
	else if (R.Op == EExtractionItemOp::Drop)
	{
		if (R.FromLoot || R.ToLoot) return TEXT("请先将物品放入背包");
		Dropped = AExtractionLootActor::SpawnContainer(GetWorld(), DropPosition(), true, Item->Definition);
		if (!Dropped) return TEXT("无法放置地面物品");
		FExtractionItemRequest Move = R;
		Move.Slot = -1;
		Move.AutoPlace = true;
		Move.MergeInto.Invalidate();
		if (!ExtractionItems::Transfer(Inv->Contents, Dropped->Contents, Move, Error))
		{
			Dropped->Destroy();
			return Error;
		}
	}
	else if (R.Op == EExtractionItemOp::Transfer)
	{
		FExtractionContainer& Destination = R.ToLoot ? Loot->Contents : Inv->Contents;
		if (!ExtractionItems::Transfer(OriginalSource, Destination, R, Error)) return Error;
	}
	else return TEXT("无效操作");
	if (!Inv->SyncWeapons())
	{
		Inv->Contents = BeforeInventory;
		if (Loot) Loot->Contents = BeforeLoot;
		if (Dropped) Dropped->Destroy();
		return TEXT("武器生成失败，操作已撤回");
	}
	if (Dropped) Dropped->Changed();
	if (Loot)
	{
		// The depositor knows their own deposited items; other viewers still search independently.
		if (R.ToLoot)
			for (const auto& I : Loot->Contents.Items)
				if (!BeforeLoot.Find(I.Id)) Revealed.FindOrAdd(Loot->Contents.Id).Add(I.Id);
		Loot->Changed();
		RefreshLootView();
	}
	Inv->Changed();
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_TRANSFER Sequence=%u Item=%s Quantity=%d Op=%d FromLoot=%d ToLoot=%d"),
	       R.Sequence, *R.Item.ToString(), R.Quantity, static_cast<int32>(R.Op), R.FromLoot, R.ToLoot);
	return {};
}

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
		if (Now >= MedicalEnd)
		{
			auto* Inv = Inventory();
			auto* I = Inv->Contents.Find(MedicalItem);
			const auto* D = I ? UExtractionItemDefinition::Get(I->Definition) : nullptr;
			if (D && C->GetExtractionHealth()->Treat(D))
			{
				Inv->Contents.Items.RemoveAll([&](const FExtractionItem& Entry) { return Entry.Id == MedicalItem; });
				++Inv->Contents.Revision;
				Inv->Changed();
				C->UpdateHUDHealth();
				C->ForceNetUpdate();
				UE_LOG(LogTemp, Display, TEXT("EXTRACTION_MEDICAL Item=%s Health=%.1f"), *MedicalItem.ToString(),
				       C->GetHealth());
			}
			CancelMedical();
		}
	}
}

void UExtractionInteractionComponent::DropOnExit()
{
	if (!GetOwner()->HasAuthority()) return;
	Close();
	CancelMedical();
	Character()->GetExtractionCombat()->CancelReload();
	Character()->GetExtractionCombat()->CancelSwap();
	if (Inventory()) Inventory()->DropAll(DropPosition());
}
