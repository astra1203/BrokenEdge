#include "ExtractionCombatComponent.h"
#include "ExtractionState.h"
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
		&& Weapon == C->GetEquippedWeapon() && Weapon->GetOwner() == C && Cast<AHitScanWeapon>(Weapon)
		&& Weapon->FireType == EFireType::EFT_HitScan && !Weapon->IsEmpty()
		&& Combat->CombatState == ECombatState::ECS_Unoccuiped && !ReloadTimer.IsValid()
		&& FMath::IsFinite(Weapon->FireDelay) && Weapon->FireDelay > 0
		&& Now + .005 >= NextShotTime && !Target.ContainsNaN()
		&& FVector::DistSquared(Target, C->GetActorLocation()) <= FMath::Square(120000.0);
	if (Valid)
	{
		NextShotTime = FMath::Max(Now, NextShotTime) + Weapon->FireDelay;
		Weapon->UseImmediateServerHitScan();
		const FVector End = Weapon->bUseScatter ? Weapon->TraceEndWithScatter(Target) : FVector(Target);
		C->PlayFireMontage(Combat->bAiming);
		Weapon->Fire(End);
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
	if (CanAct()) Character()->GetCombat()->Drop();
}

void UExtractionCombatComponent::RequestReload()
{
	auto* C = Character();
	UCombatComponent* Combat = C ? C->GetCombat().Get() : nullptr;
	auto* W = C ? C->GetEquippedWeapon() : nullptr;
	if (!CanAct() || !W || W->IsFull() || Combat->CarriedAmmo <= 0 || Combat->bLocallyReloading
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
		|| Combat->CarriedAmmo <= 0 || Combat->CombatState != ECombatState::ECS_Unoccuiped || ReloadTimer.IsValid())
	{
		ClientReloadRejected();
		return;
	}
	ReloadWeapon = W;
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
		const int32 Amount = Combat->AmountToReload();
		Combat->CarriedAmmoMap.FindOrAdd(W->GetWeaponType()) -= Amount;
		Combat->CarriedAmmo = Combat->CarriedAmmoMap[W->GetWeaponType()];
		W->SetExtractionAmmo(W->GetAmmo() + Amount);
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

void UExtractionCombatComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);
	Super::EndPlay(Reason);
}
