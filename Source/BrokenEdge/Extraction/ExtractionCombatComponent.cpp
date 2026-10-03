#include "ExtractionCombatComponent.h"
#include "ExtractionState.h"
#include "ExtractionInteractionComponent.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionPlayerController.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Weapon/HitScanWeapon.h"
#include "Animation/AnimMontage.h"
#include "TimerManager.h"

UExtractionCombatComponent::UExtractionCombatComponent() { SetIsReplicatedByDefault(true); }
ABrokenEdgeCharacter* UExtractionCombatComponent::Character() const { return Cast<ABrokenEdgeCharacter>(GetOwner()); }

bool UExtractionCombatComponent::IsEnabled() const
{
	return GetWorld() && GetWorld()->GetGameState<AExtractionGameState>();
}

bool UExtractionCombatComponent::CanAct() const
{
	const auto* C = Character();
	const auto* GS = GetWorld() ? GetWorld()->GetGameState<AExtractionGameState>() : nullptr;
	const auto* PS = C ? C->GetPlayerState<AExtractionPlayerState>() : nullptr;
	return C && GS && GS->Phase == EExtractionPhase::Active && PS && PS->Life == EExtractionLife::Alive
		&& !C->IsDefeated() && !C->bDisableGameplay;
}

void UExtractionCombatComponent::RequestShot(const FVector& Target)
{
	auto* C = Character();
	auto* W = C ? C->GetEquippedWeapon() : nullptr;
	if (!CanAct() || !Cast<AHitScanWeapon>(W)) return;
	if (C->GetExtractionInteraction()->InventoryOpen) return;
	const uint32 Id = ++LocalShotId;
	if (!C->HasAuthority())
	{
		PendingShots.Add(Id, W);
		C->GetCombat()->LocalFire(Target);
	}
	ServerRequestShot(Id, W, Target);
}

void UExtractionCombatComponent::ServerRequestShot_Implementation(uint32 ShotId, AWeapon* Weapon,
                                                                  FVector_NetQuantize Target)
{
	if (!IsEnabled()) return;
	auto* C = Character();
	UCombatComponent* Combat = C ? C->GetCombat().Get() : nullptr;
	const double Now = GetWorld()->GetTimeSeconds();
	const bool Fresh = ShotId > LastProcessedShot;
	if (Fresh) LastProcessedShot = ShotId;
	const bool Valid = IsEnabled() && Fresh && CanAct() && Combat && IsValid(Weapon)
		&& !C->GetExtractionInteraction()->InventoryOpen
		&& Weapon == C->GetEquippedWeapon() && Weapon->GetOwner() == C && Cast<AHitScanWeapon>(Weapon)
		&& Weapon->FireType == EFireType::EFT_HitScan && !Weapon->IsEmpty()
		&& Combat->CombatState == ECombatState::ECS_Unoccuiped && !ReloadTimer.IsValid()
		&& FMath::IsFinite(Weapon->FireDelay) && Weapon->FireDelay > 0
		&& Now + .005 >= NextShotTime && !Target.ContainsNaN()
		&& FVector::DistSquared(Target, C->GetActorLocation()) <= FMath::Square(120000.0);
	if (Valid)
	{
		C->GetExtractionInteraction()->CancelMedical();
		NextShotTime = FMath::Max(Now, NextShotTime) + Weapon->FireDelay;
		Weapon->UseImmediateServerHitScan();
		const FVector End = Weapon->bUseScatter ? Weapon->TraceEndWithScatter(Target) : FVector(Target);
		C->PlayFireMontage(Combat->bAiming);
		Weapon->Fire(End);
		if (auto* Inventory = C->GetExtractionInteraction()->Inventory()) Inventory->RecordShot(Weapon);
		++AcceptedShots;
		MulticastShotEffects(Weapon, End, Weapon->GetAmmo());
	}
	else ++RejectedShots;
	// Never expose or overwrite an unrelated player's weapon from a forged request.
	AWeapon* OwnedWeapon = IsValid(Weapon) && Weapon->GetOwner() == C ? Weapon : (C ? C->GetEquippedWeapon() : nullptr);
	ClientShotResult(LastProcessedShot, OwnedWeapon, OwnedWeapon ? OwnedWeapon->GetAmmo() : 0, Valid);
}

void UExtractionCombatComponent::ClientShotResult_Implementation(uint32 ShotId, AWeapon* Weapon, int32 Ammo,
                                                                 bool bAccepted)
{
	if (GetOwner()->HasAuthority() || ShotId < LastAcknowledgedShot) return;
	LastAcknowledgedShot = ShotId;
	for (auto It = PendingShots.CreateIterator(); It; ++It)
		if (It.Key() <= ShotId) It.RemoveCurrent();
	int32 Predicted = 0;
	for (const auto& Entry : PendingShots) if (Entry.Value.Get() == Weapon) ++Predicted;
	if (IsValid(Weapon)) Weapon->SetExtractionAmmo(Ammo - Predicted);
}

void UExtractionCombatComponent::MulticastShotEffects_Implementation(AWeapon* Weapon, FVector_NetQuantize Target,
                                                                     int32 Ammo)
{
	auto* C = Character();
	if (!C || C->HasAuthority() || !IsValid(Weapon) || C->IsLocallyControlled()) return;
	C->PlayFireMontage(C->IsAiming());
	Weapon->Fire(Target); // Clients produce effects only; rewind requests are disabled for this mode.
	Weapon->SetExtractionAmmo(Ammo);
}

void UExtractionCombatComponent::ServerDropWeapon_Implementation()
{
	// Legacy endpoint must still enter the inventory transaction path.
	if (CanAct())
		if (auto* PC = Cast<AExtractionPlayerController>(Character()->GetController())) PC->DropEquippedItem();
}

void UExtractionCombatComponent::RequestReload()
{
	auto* C = Character();
	UCombatComponent* Combat = C ? C->GetCombat().Get() : nullptr;
	auto* W = C ? C->GetEquippedWeapon() : nullptr;
	if (!CanAct() || !W || W->IsFull() || Combat->CarriedAmmo <= 0 || Combat->bLocallyReloading
		|| C->GetExtractionInteraction()->InventoryOpen
		|| Combat->CombatState != ECombatState::ECS_Unoccuiped)
		return;
	Combat->bLocallyReloading = true;
	Combat->HandleReload();
	Combat->ServerReload();
}

void UExtractionCombatComponent::BeginServerReload()
{
	auto* C = Character();
	UCombatComponent* Combat = C ? C->GetCombat().Get() : nullptr;
	auto* W = C ? C->GetEquippedWeapon() : nullptr;
	if (!CanAct() || !W || W->GetOwner() != C || !Cast<AHitScanWeapon>(W) || W->IsFull()
		|| C->GetExtractionInteraction()->InventoryOpen
		|| Combat->CarriedAmmo <= 0 || Combat->CombatState != ECombatState::ECS_Unoccuiped || ReloadTimer.IsValid())
	{
		ClientReloadRejected();
		return;
	}
	ReloadWeapon = W;
	C->GetExtractionInteraction()->CancelMedical();
	Combat->CombatState = ECombatState::ECS_Reloading;
	if (!C->IsLocallyControlled()) Combat->HandleReload();
	float Duration = 2.f;
	if (const UAnimMontage* Montage = C->GetReloadMontage())
	{
		const int32 RifleSection = Montage->GetSectionIndex(TEXT("Rifle"));
		// The shared montage also contains shotgun sections which are not played by the AK.
		Duration = RifleSection != INDEX_NONE ? Montage->GetSectionLength(RifleSection) : Montage->GetPlayLength();
	}
	GetWorld()->GetTimerManager().SetTimer(ReloadTimer, this, &ThisClass::FinishServerReload,
	                                       FMath::Max(.25f, Duration));
}

void UExtractionCombatComponent::FinishServerReload()
{
	ReloadTimer.Invalidate();
	auto* C = Character();
	UCombatComponent* Combat = C ? C->GetCombat().Get() : nullptr;
	AWeapon* W = ReloadWeapon.Get();
	if (CanAct() && W && W == C->GetEquippedWeapon() && W->GetOwner() == C && Combat->CombatState ==
		ECombatState::ECS_Reloading)
	{
		if (auto* Inventory = C->GetExtractionInteraction()->Inventory()) Inventory->Reload(W);
		// Keep reload and shot snapshots on this actor's reliable channel so an old
		// shot cannot arrive after a newer refill and restore a stale ammo count.
		MulticastReloadAmmo(W, W->GetAmmo());
		ClientShotResult(LastProcessedShot, W, W->GetAmmo(), true);
		Combat->UpdateCarriedAmmo();
	}
	CancelReload();
}

void UExtractionCombatComponent::MulticastReloadAmmo_Implementation(AWeapon* Weapon, int32 Ammo)
{
	if (!GetOwner()->HasAuthority() && IsValid(Weapon)) Weapon->SetExtractionAmmo(Ammo);
}

void UExtractionCombatComponent::CancelReload()
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);
	ReloadWeapon.Reset();
	if (auto* C = Character())
	{
		UCombatComponent* Combat = C->GetCombat();
		if (Combat->CombatState == ECombatState::ECS_Reloading) Combat->CombatState = ECombatState::ECS_Unoccuiped;
		Combat->bLocallyReloading = false;
		if (C->HasAuthority()) ClientReloadRejected();
	}
}

void UExtractionCombatComponent::ClientReloadRejected_Implementation()
{
	if (auto* C = Character()) C->GetCombat()->bLocallyReloading = false;
}

void UExtractionCombatComponent::BeginServerSwap()
{
	auto* C = Character();
	if (!C || !C->HasAuthority() || !CanAct() || C->GetExtractionInteraction()->InventoryOpen) return;
	auto* Combat = C->GetCombat().Get();
	if (Combat->CombatState != ECombatState::ECS_Unoccuiped || !Combat->ShouldSwapWeapons()) return;
	CancelSwap();
	C->GetExtractionInteraction()->CancelMedical();
	SwapPrimary = Combat->EquippedWeapon;
	SwapSecondary = Combat->SecondaryWeapon;
	float AttachTime = .75f, FinishTime = 1.35f;
	if (const auto* Montage = C->GetSwapMontage())
		for (const FAnimNotifyEvent& Notify : Montage->Notifies)
		{
			const FString Name = Notify.GetNotifyEventName().ToString();
			if (Name.Contains(TEXT("AttachWeapons"))) AttachTime = Notify.GetTriggerTime();
			if (Name.Contains(TEXT("SwapFinished"))) FinishTime = Notify.GetTriggerTime();
		}
	AttachTime = FMath::Max(.05f, AttachTime);
	FinishTime = FMath::Max(AttachTime + .05f, FinishTime);
	Combat->CombatState = ECombatState::ECS_SwappingWeapon;
	C->bFinishedSwapping = false;
	Combat->SecondaryWeapon->EnableCustomDepth(false);
	MulticastSwapStarted();
	C->ForceNetUpdate();
	// Authority does not depend on a rendered montage firing every notify.
	GetWorld()->GetTimerManager().SetTimer(SwapAttachTimer, this, &ThisClass::CommitServerSwap, AttachTime);
	GetWorld()->GetTimerManager().SetTimer(SwapFinishTimer, this, &ThisClass::FinishServerSwap, FinishTime);
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_SWAP_SCHEDULE Attach=%.3f Finish=%.3f"), AttachTime, FinishTime);
}

void UExtractionCombatComponent::MulticastSwapStarted_Implementation()
{
	if (auto* C = Character()) C->PlaySwapMontage();
}

void UExtractionCombatComponent::CommitServerSwap()
{
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_SWAP_COMMIT"));
	SwapAttachTimer.Invalidate();
	auto* C = Character();
	if (!C || !CanAct())
	{
		CancelSwap();
		return;
	}
	auto* Combat = C->GetCombat().Get();
	if (Combat->CombatState != ECombatState::ECS_SwappingWeapon || !SwapPrimary.IsValid()
		|| !SwapSecondary.IsValid() || Combat->EquippedWeapon != SwapPrimary.Get()
		|| Combat->SecondaryWeapon != SwapSecondary.Get())
	{
		CancelSwap();
		return;
	}
	Combat->AttachSwappedWeapons();
	SwapPrimary.Reset();
	SwapSecondary.Reset();
	C->ForceNetUpdate();
}

void UExtractionCombatComponent::FinishServerSwap() { CancelSwap(); }

void UExtractionCombatComponent::CancelSwap()
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(SwapAttachTimer);
		GetWorld()->GetTimerManager().ClearTimer(SwapFinishTimer);
	}
	SwapPrimary.Reset();
	SwapSecondary.Reset();
	if (auto* C = Character())
	{
		auto* Combat = C->GetCombat().Get();
		if (Combat->CombatState == ECombatState::ECS_SwappingWeapon) Combat->CombatState = ECombatState::ECS_Unoccuiped;
		C->bFinishedSwapping = true;
		if (Combat->SecondaryWeapon) Combat->SecondaryWeapon->EnableCustomDepth(true);
		C->ForceNetUpdate();
	}
}

void UExtractionCombatComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	CancelSwap();
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);
	Super::EndPlay(Reason);
}
