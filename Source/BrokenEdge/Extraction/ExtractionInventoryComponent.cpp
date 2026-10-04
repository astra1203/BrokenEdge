#include "ExtractionInventoryComponent.h"
#include "ExtractionCombatComponent.h"
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

UExtractionInventoryComponent::UExtractionInventoryComponent() { SetIsReplicatedByDefault(true); }

void UExtractionInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UExtractionInventoryComponent, Contents, COND_OwnerOnly);
}

ABrokenEdgeCharacter* UExtractionInventoryComponent::Character() const
{
	const auto* PS = Cast<AExtractionPlayerState>(GetOwner());
	return PS ? Cast<ABrokenEdgeCharacter>(PS->GetPawn()) : Cast<ABrokenEdgeCharacter>(GetOwner());
}

void UExtractionInventoryComponent::InitializeLoadout()
{
	if (!GetOwner()->HasAuthority() || (bInitialized && InitializedWorld == GetWorld())) return;
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
	SyncWeapons();
	Changed();
}

void UExtractionInventoryComponent::InitializeGuard()
{
	auto* C = Character();
	if (!C || !C->HasAuthority() || !C->bExtractionGuard || bInitialized) return;
	bInitialized = true;
	Contents.Id = FGuid::NewGuid();
	Contents.WeaponSlots = true;
	auto Gun = ExtractionItems::Make(TEXT("AK"));
	Gun.Slot = 0;
	Gun.Magazine = 30;
	ExtractionItems::SeedOrigins(Gun);
	Contents.Items.Add(Gun);
	ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(TEXT("Ammo"), 60));
	ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(TEXT("Medkit")));
	SyncWeapons();
	Changed();
}

void UExtractionInventoryComponent::ClearSettled()
{
	if (!GetOwner()->HasAuthority()) return;
	bDropped = true;
	Contents.Items.Reset();
	++Contents.Revision;
	SyncWeapons();
	Changed();
}

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
		const auto* State = Contents.InSlot(Slot);
		Desired[Slot]->ExtractionAmmoType = State->MagazineAmmo;
		Desired[Slot]->ExtractionParts.Reset();
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
		ExtractionItems::TakeOrigins(I->MagazineOrigins, 1);
		Weapon->ExtractionInitialAmmo = Weapon->GetAmmo();
		++Contents.Revision;
		GetOwner()->ForceNetUpdate();
	}
}

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
