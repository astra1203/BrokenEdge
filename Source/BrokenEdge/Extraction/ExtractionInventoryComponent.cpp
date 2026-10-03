#include "ExtractionInventoryComponent.h"
#include "ExtractionState.h"
#include "ExtractionLootActor.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "BrokenEdge/BrokenEdgeTypes/WeaponType.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

UExtractionInventoryComponent::UExtractionInventoryComponent() { SetIsReplicatedByDefault(true); }

void UExtractionInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UExtractionInventoryComponent, Contents, COND_OwnerOnly);
}

ABrokenEdgeCharacter* UExtractionInventoryComponent::Character() const
{
	const auto* PS = Cast<AExtractionPlayerState>(GetOwner());
	return PS ? Cast<ABrokenEdgeCharacter>(PS->GetPawn()) : nullptr;
}

void UExtractionInventoryComponent::InitializeLoadout()
{
	if (!GetOwner()->HasAuthority() || (bInitialized && InitializedWorld == GetWorld())) return;
	bInitialized = true;
	InitializedWorld = GetWorld();
	bDropped = false;
	Contents = FExtractionContainer();
	Contents.Id = FGuid::NewGuid();
	Contents.WeaponSlots = true;
	for (int32 I = 0; I < 2; ++I)
	{
		ensure(ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(TEXT("Ammo"), 60)));
		ensure(ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(TEXT("Medkit"))));
	}
	Changed();
}

void UExtractionInventoryComponent::SyncReserve()
{
	if (auto* C = Character())
	{
		auto* Combat = C->GetCombat().Get();
		Combat->CarriedAmmo = Contents.Count(TEXT("Ammo"));
		Combat->CarriedAmmoMap.FindOrAdd(EWeaponType::EWT_AssaultRifle) = Combat->CarriedAmmo;
		Combat->UpdateCarriedAmmo();
	}
}

void UExtractionInventoryComponent::OnRep_Contents() { SyncReserve(); }

void UExtractionInventoryComponent::Changed()
{
	SyncReserve();
	GetOwner()->ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_INVENTORY Owner=%s Container=%s Revision=%d Items=%d Ammo=%d"),
	       *GetOwner()->GetName(), *Contents.Id.ToString(), Contents.Revision, Contents.Items.Num(),
	       Contents.Count(TEXT("Ammo")));
}

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
			Created.Add(Desired[Slot]);
		}
	}
	AWeapon* Old[2] = {Combat->EquippedWeapon, Combat->SecondaryWeapon};
	Combat->EquippedWeapon = Desired[0];
	Combat->SecondaryWeapon = Desired[1];
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		if (!Desired[Slot]) continue;
		if (Created.Contains(Desired[Slot]))
		{
			Desired[Slot]->ExtractionInitialAmmo = Contents.InSlot(Slot)->Magazine;
			Desired[Slot]->SetExtractionAmmo(Contents.InSlot(Slot)->Magazine);
		}
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

void UExtractionInventoryComponent::RecordShot(AWeapon* Weapon)
{
	if (!GetOwner()->HasAuthority() || !Weapon) return;
	if (auto* I = Contents.Find(Weapon->ExtractionItemId))
	{
		I->Magazine = Weapon->GetAmmo();
		Weapon->ExtractionInitialAmmo = Weapon->GetAmmo();
		++Contents.Revision;
		GetOwner()->ForceNetUpdate();
	}
}

int32 UExtractionInventoryComponent::Reload(AWeapon* Weapon)
{
	if (!GetOwner()->HasAuthority() || !Weapon || !Contents.Find(Weapon->ExtractionItemId)) return 0;
	int32 Amount = FMath::Min(Weapon->GetMagCapacity() - Weapon->GetAmmo(), Contents.Count(TEXT("Ammo")));
	int32 Remaining = Amount;
	for (auto& I : Contents.Items)
		if (I.Definition == TEXT("Ammo"))
		{
			const int32 Take = FMath::Min(Remaining, I.Quantity);
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

void UExtractionInventoryComponent::DropAll(const FVector& Position)
{
	if (!GetOwner()->HasAuthority() || bDropped) return;
	CaptureWeapons();
	if (!Contents.Items.IsEmpty())
	{
		auto* Loot = AExtractionLootActor::SpawnContainer(GetWorld(), Position, false);
		if (!Loot)
		{
			UE_LOG(LogTemp, Error, TEXT("EXTRACTION_DROP_FAILED Owner=%s"), *GetOwner()->GetName());
			return;
		}
		FExtractionContainer Dropped = Contents;
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
