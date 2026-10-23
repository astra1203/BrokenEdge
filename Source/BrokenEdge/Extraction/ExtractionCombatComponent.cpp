#include "ExtractionCombatComponent.h"
#include "ExtractionMeleeComponent.h"
#include "ExtractionState.h"
#include "ExtractionInteractionComponent.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionPlayerController.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Weapon/HitScanWeapon.h"
#include "Animation/AnimMontage.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DrawDebugHelpers.h"
#include "Camera/CameraComponent.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarWeaponStats(
	TEXT("BrokenEdge.Weapon.DebugStats"), 0, TEXT("Show ADS, breath, spread and source revision."));

void UExtractionCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UExtractionCombatComponent, MechanicsSeed, COND_OwnerOnly);
}

void UExtractionCombatComponent::RefreshMechanics()
{
	auto* C = Character();
	auto* W = C ? C->GetEquippedWeapon() : nullptr;
	if (W == MechanicsWeapon.Get() && (Stats.IsValid() || !W) && (!W || (MechanicsParts == W->ExtractionParts &&
		Configuration == W->ExtractionConfiguration))) return;
	MechanicsParts = W ? W->ExtractionParts : TArray<FName>();
	Configuration = W ? W->ExtractionConfiguration : 0;
	MechanicsWeapon = W;
	Stats = {};
	ResetTransientMechanics();
	if (W)
	{
		FExtractionItem Item;
		Item.Definition = W->ExtractionDefinition;
		for (FName Part : W->ExtractionParts)
		{
			FExtractionAttachment A;
			A.Definition = Part;
			Item.Attachments.Add(A);
		}
		if (!W->ExtractionAssembly.IsEmpty()) Item.Attachments = W->ExtractionAssembly;
		Stats = WeaponMechanics::ResolveWeaponStats(Item);
		if (Stats.IsValid())
		{
			W->FireDelay = Stats.Interval;
			ReadyAt = GetWorld()->GetTimeSeconds() + Stats.Equip;
		}
	}
	if (C && C->HasAuthority() && !MechanicsSeed) MechanicsSeed = FMath::RandHelper(MAX_int32 - 1) + 1;
}

bool UExtractionCombatComponent::CanFireMechanics() const
{
	const auto* C = Character();
	return !Stats.IsValid() || (C && !C->IsSprinting() && GetWorld()->GetTimeSeconds() >= ReadyAt);
}

void UExtractionCombatComponent::ResetTransientMechanics()
{
	AimAlpha = 0;
	BurstIndex = 0;
	SpreadAccumulation = FVector2D::ZeroVector;
	LastMechanicsShot = -100;
	VisualKick = 0;
	RecoilDebt = FVector2D::ZeroVector;
	bHaveControl = false;
	bBreathHeld = false;
	// Breath and cooldown belong to the player; changing weapons must not refill them.
	if (auto* C = Character(); C && C->IsLocallyControlled() && C->GetFollowCamera())
		C->GetFollowCamera()->ClearAdditiveOffset();
}

bool UExtractionCombatComponent::CanSteadyWeapon() const
{
	const auto* C = Character();
	return CanAct() && C && !C->IsSprinting() && !C->Melee->Active
		&& !C->GetExtractionInteraction()->InventoryOpen
		&& !C->GetCombat()->bLocallyReloading
		&& C->GetCombat()->CombatState == ECombatState::ECS_Unoccuiped;
}

void UExtractionCombatComponent::HoldBreathPressed()
{
	bBreathHeld = CanSteadyWeapon() && Character()->IsAiming();
	ServerHoldBreath(bBreathHeld);
}

void UExtractionCombatComponent::HoldBreathReleased()
{
	bBreathHeld = false;
	ServerHoldBreath(false);
}

void UExtractionCombatComponent::ServerHoldBreath_Implementation(bool bHeld)
{
	bBreathHeld = bHeld && CanSteadyWeapon() && Character()->IsAiming();
}

void UExtractionCombatComponent::TickComponent(float Delta, ELevelTick Kind, FActorComponentTickFunction* Tick)
{
	Super::TickComponent(Delta, Kind, Tick);
	if (!IsEnabled()) return;
	RefreshMechanics();
	auto* C = Character();
	if (C && C->IsLocallyControlled() && C->GetFollowCamera()) C->GetFollowCamera()->ClearAdditiveOffset();
	if (!C || !Stats.IsValid()) return;
	const double Now = GetWorld()->GetTimeSeconds();
	const bool Steady = CanSteadyWeapon();
	if (!Steady) ResetTransientMechanics();
	const bool Aiming = Steady && C->IsAiming();
	if (!Aiming) bBreathHeld = false;
	AimAlpha = FMath::FInterpConstantTo(AimAlpha, Aiming ? 1.f : 0.f, Delta,
	                                    1.f / FMath::Max(.01f, Aiming ? Stats.AimIn : Stats.AimOut));
	if (C->IsSprinting()) ReadyAt = Now + Stats.SprintToFire;
	const bool Hold = CanAct() && bBreathHeld && AimAlpha > .99f && Breath > 0 && Now >= BreathResume;
	Breath = FMath::Clamp(Breath + Delta * (Hold ? -Stats.BreathDrain : Stats.BreathRecovery), 0.f, 100.f);
	if (Hold && Breath <= 0) BreathResume = Now + Stats.BreathCooldown;
	const FVector2D Recover = FMath::Lerp(Stats.Spread[0].Recovery, Stats.Spread[1].Recovery, AimAlpha) * Delta;
	SpreadAccumulation.X = FMath::Max(0., SpreadAccumulation.X - Recover.X);
	SpreadAccumulation.Y = FMath::Max(0., SpreadAccumulation.Y - Recover.Y);
	VisualKick *= FMath::Exp(-Stats.KickRecovery * Delta);
	if (C->IsLocallyControlled() && C->GetFollowCamera() && CanAct())
	{
		// Compatibility camera impulse, deliberately excluded from the authoritative aim ray.
		const float CameraPitch = VisualKick * Stats.CameraKickScale * FMath::Sin(
			(Now - LastMechanicsShot) * Stats.CameraFrequency);
		C->GetFollowCamera()->AddAdditiveOffset(FTransform(FRotator(CameraPitch, 0, 0)), 0);
	}
	if (C->IsLocallyControlled() && C->Controller)
	{
		FRotator Control = C->Controller->GetControlRotation();
		if (bHaveControl)
		{
			const auto Input = (Control - PreviousControl).GetNormalized();
			RecoilDebt.X = WeaponMechanics::CompensatedDebt(RecoilDebt.X, Input.Pitch);
			RecoilDebt.Y = WeaponMechanics::CompensatedDebt(RecoilDebt.Y, Input.Yaw);
		}
		if (Now - LastMechanicsShot > Stats.Interval * Stats.BurstGapIntervals)
		{
			const FVector2D RecoverDebt = RecoilDebt * (1.f - FMath::Exp(-Stats.RecoilRecovery * Delta));
			Control.Pitch -= RecoverDebt.X;
			Control.Yaw -= RecoverDebt.Y;
			RecoilDebt -= RecoverDebt;
			C->Controller->SetControlRotation(Control);
		}
		PreviousControl = Control;
		bHaveControl = true;
	}
	if (C->IsLocallyControlled() && CanFireMechanics() && C->GetCombat()->bFireButtonPressed
		&& C->GetCombat()->bCanFire && MechanicsWeapon.IsValid() && MechanicsWeapon->bAutomatic
		&& !C->GetExtractionInteraction()->InventoryOpen)
		C->GetCombat()->Fire();
	if (CVarWeaponStats.GetValueOnGameThread() && C->IsLocallyControlled())
		DrawDebugString(GetWorld(), C->GetActorLocation() + FVector(0, 0, 100), FString::Printf(
			                TEXT("%s rev=%u ADS=%.2f Breath=%.1f Spread=%s RPM=%.1f [compatibility]"),
			                *Stats.Definition.ToString(), Stats.Revision, AimAlpha, Breath,
			                *SpreadAccumulation.ToString(), 60 / Stats.Interval), nullptr, FColor::Green, 0);
}

FVector UExtractionCombatComponent::ResolveShot(const FVector& Target, uint32 Id, bool bApplyInput)
{
	auto* C = Character();
	auto* W = MechanicsWeapon.Get();
	if (!C || !W || !Stats.IsValid()) return Target;
	const double Now = GetWorld()->GetTimeSeconds();
	const bool Continuous = Now - LastMechanicsShot < FMath::Max(.2f, Stats.Interval * Stats.BurstGapIntervals);
	if (!Continuous) BurstIndex = 0;
	const FVector Start = W->GetWeaponMesh()->GetSocketLocation(TEXT("MuzzleFlash"));
	FVector2D Spread = FMath::Lerp(Stats.Spread[0].Base, Stats.Spread[1].Base, AimAlpha) + SpreadAccumulation;
	Spread *= Stats.SpreadScale;
	if (C->GetVelocity().SizeSquared2D() > 100) Spread *= Stats.MoveSpread;
	if (C->bIsCrouched) Spread *= Stats.CrouchSpread;
	if (C->GetCharacterMovement()->IsFalling()) Spread *= Stats.JumpSpread;
	const uint32 Seed = HashCombine(MechanicsSeed, Id);
	const bool Hold = CanSteadyWeapon() && bBreathHeld && AimAlpha > .99f && Breath > 0 && Now >= BreathResume;
	// Continuous deterministic sway, separate from random spread. Frequency/amplitude are compatibility tuning.
	const float Phase = GetWorld()->GetGameState()->GetServerWorldTimeSeconds();
	const float Sway = AimAlpha * (C->bIsCrouched ? Stats.CrouchBreath : Stats.StandBreath) * (
		Hold ? Stats.HeldSwayScale : 1.f) * Stats.SwayDegrees;
	FRotator Aim = (Target - Start).Rotation();
	Aim.Pitch += FMath::Sin(Phase * Stats.SwayPitchFrequency) * Sway;
	Aim.Yaw += FMath::Sin(Phase * Stats.SwayYawFrequency) * Sway;
	const FVector End = Start + WeaponMechanics::SpreadDirection(Aim.Vector(), Spread, Seed) * Stats.MaxDistance;
	auto Recoil = Stats.Recoil(BurstIndex++, Continuous, AimAlpha, Seed) * (Hold ? Stats.HeldRecoilScale : 1.f);
	if (C->bIsCrouched) Recoil *= Stats.CrouchRecoil;
	if (bApplyInput && C->IsLocallyControlled() && C->Controller)
	{
		FRotator Control = C->Controller->GetControlRotation();
		Control.Pitch += Recoil.X;
		Control.Yaw += Recoil.Y;
		C->Controller->SetControlRotation(Control);
		RecoilDebt += Recoil;
		PreviousControl = Control;
		bHaveControl = true;
	}
	VisualKick += Recoil.X * Stats.KickPerShot;
	SpreadAccumulation += FMath::Lerp(Stats.Spread[0].PerShot, Stats.Spread[1].PerShot, AimAlpha);
	const auto Max = FMath::Lerp(Stats.Spread[0].Maximum, Stats.Spread[1].Maximum, AimAlpha);
	SpreadAccumulation.X = FMath::Min(SpreadAccumulation.X, Max.X);
	SpreadAccumulation.Y = FMath::Min(SpreadAccumulation.Y, Max.Y);
	Breath = FMath::Max(0.f, Breath - Stats.BreathShot);
	BreathResume = Now + Stats.BreathCooldown;
	LastMechanicsShot = Now;
	return End;
}

UExtractionCombatComponent::UExtractionCombatComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
}

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
	return C && GS && GS->Phase == EExtractionPhase::Active && (C->bExtractionGuard || (PS && PS->Life ==
			EExtractionLife::Alive))
		&& !C->IsDefeated() && !C->bDisableGameplay && !C->IsTraversing();
}

void UExtractionCombatComponent::FireGuard(const FVector& Target)
{
	auto* C = Character();
	if (!C || !C->HasAuthority() || !C->bExtractionGuard) return;
	ServerRequestShot(++LocalShotId, C->GetEquippedWeapon(), Target);
}

void UExtractionCombatComponent::RequestShot(const FVector& Target)
{
	RefreshMechanics();
	auto* C = Character();
	auto* W = C ? C->GetEquippedWeapon() : nullptr;
	if (!CanAct() || !CanFireMechanics() || C->Melee->Active || !Cast<AHitScanWeapon>(W)) return;
	if (C->GetExtractionInteraction()->InventoryOpen) return;
	const uint32 Id = ++LocalShotId;
	if (!C->HasAuthority())
	{
		PendingShots.Add(Id, W);
		C->GetCombat()->LocalFire(ResolveShot(Target, Id, true));
	}
	ServerRequestShot(Id, W, Target);
}

void UExtractionCombatComponent::ServerRequestShot(uint32 ShotId, AWeapon* Weapon, FVector_NetQuantize Target)
{
	const auto* Profile = Weapon ? WeaponMechanics::Find(Weapon->ExtractionDefinition) : nullptr;
	ServerRequestShotVersioned(ShotId, Weapon, Target,
	                           Weapon && Weapon->ExtractionConfiguration
		                           ? Weapon->ExtractionConfiguration
		                           : Profile
		                           ? Profile->Revision
		                           : 0);
}

void UExtractionCombatComponent::ServerRequestShotVersioned_Implementation(uint32 ShotId, AWeapon* Weapon,
                                                                           FVector_NetQuantize Target, uint32 Revision)
{
	if (!IsEnabled()) return;
	RefreshMechanics();
	auto* C = Character();
	UCombatComponent* Combat = C ? C->GetCombat().Get() : nullptr;
	const double Now = GetWorld()->GetTimeSeconds();
	const bool Fresh = ShotId > LastProcessedShot;
	if (Fresh) LastProcessedShot = ShotId;
	const bool Valid = IsEnabled() && Fresh && CanAct() && CanFireMechanics() && Combat && IsValid(Weapon)
		&& !C->Melee->Active
		&& !C->GetExtractionInteraction()->InventoryOpen
		&& Weapon == C->GetEquippedWeapon() && Weapon->GetOwner() == C && Cast<AHitScanWeapon>(Weapon)
		&& (!Stats.IsValid() || (Revision == Stats.Revision && (C->bExtractionGuard ||
			FVector::DotProduct((FVector(Target) - C->GetPawnViewLocation()).GetSafeNormal(),
			                    C->GetBaseAimRotation().Vector()) > .7f)))
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
		float SavedScatter = Weapon->SphereRadius;
		if (const auto* Inventory = C->GetExtractionInteraction()->Inventory())
			if (const auto* Item = Inventory->Contents.Find(Weapon->ExtractionItemId))
				Weapon->SphereRadius *= ExtractionItems::ScatterScale(*Item);
		const FVector End = Stats.IsValid()
			                    ? ResolveShot(Target, ShotId, true)
			                    : Weapon->bUseScatter
			                    ? Weapon->TraceEndWithScatter(Target)
			                    : FVector(Target);
		Weapon->SphereRadius = SavedScatter;
		C->PlayFireMontage(Combat->bAiming);
		Weapon->Fire(End);
		if (auto* Inventory = C->GetExtractionInteraction()->Inventory()) Inventory->RecordShot(Weapon);
		++AcceptedShots;
		MulticastShotEffects(Weapon, End, Weapon->GetAmmo());
	}
	else ++RejectedShots;
	// Never expose or overwrite an unrelated player's weapon from a forged request.
	AWeapon* OwnedWeapon = IsValid(Weapon) && Weapon->GetOwner() == C ? Weapon : (C ? C->GetEquippedWeapon() : nullptr);
	if (!C || !C->bExtractionGuard)
		ClientShotResult(LastProcessedShot, OwnedWeapon,
		                 OwnedWeapon ? OwnedWeapon->GetAmmo() : 0, Valid);
	if (C && !C->bExtractionGuard)
		ClientMechanicsResult(LastProcessedShot, OwnedWeapon, BurstIndex,
		                      SpreadAccumulation, Breath, Now, LastMechanicsShot);
}

void UExtractionCombatComponent::ClientMechanicsResult_Implementation(uint32 ShotId, AWeapon* Weapon, uint32 Sequence,
                                                                      FVector2D Spread, float InBreath,
                                                                      double ServerTime, double ShotTime)
{
	if (GetOwner()->HasAuthority() || Weapon != MechanicsWeapon.Get() || ShotId < LastAcknowledgedShot) return;
	uint32 Pending = 0;
	for (const auto& Entry : PendingShots) if (Entry.Value.Get() == Weapon) ++Pending;
	BurstIndex = Sequence + Pending;
	SpreadAccumulation = Spread + FMath::Lerp(Stats.Spread[0].PerShot, Stats.Spread[1].PerShot, AimAlpha) * Pending;
	Breath = FMath::Clamp(InBreath - Pending * Stats.BreathShot, 0.f, 100.f);
	if (!Pending) LastMechanicsShot = GetWorld()->GetTimeSeconds() - FMath::Max(0., ServerTime - ShotTime);
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
	if (Character() && Character()->Melee->Active) return;
	// Legacy endpoint must still enter the inventory transaction path.
	if (CanAct())
		if (auto* PC = Cast<AExtractionPlayerController>(Character()->GetController())) PC->DropEquippedItem();
}

void UExtractionCombatComponent::RequestReload()
{
	if (Character() && Character()->Melee->Active) return;
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
	if (Character() && Character()->Melee->Active) return;
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
	                                       FMath::Max(.25f, Stats.IsValid()
		                                                        ? (W->IsEmpty() ? Stats.EmptyReload : Stats.Reload)
		                                                        : Duration));
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

void UExtractionCombatComponent::EquipmentAmmo(AWeapon* Weapon, int32 Ammo)
{
	if (!GetOwner()->HasAuthority() || !Weapon) return;
	Weapon->SetExtractionAmmo(Ammo);
	Weapon->ExtractionInitialAmmo = Ammo;
	MulticastReloadAmmo(Weapon, Ammo);
	ClientShotResult(LastProcessedShot, Weapon, Ammo, true);
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
	if (Character() && Character()->Melee->Active) return;
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
	RefreshMechanics();
	if (Stats.IsValid())
		if (const auto* Next = WeaponMechanics::Find(Combat->SecondaryWeapon->ExtractionDefinition))
		{
			AttachTime = Stats.Unequip;
			FinishTime = AttachTime + Next->Equip;
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
	ResetTransientMechanics();
	CancelSwap();
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);
	Super::EndPlay(Reason);
}
