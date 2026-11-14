#include "CombatModeAdapter.h"
#include "CombatComponent.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Extraction/Combat/ExtractionCombatComponent.h"
#include "BrokenEdge/Extraction/Core/ExtractionPlayerController.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "TimerManager.h"

class FLegacyCombatModeAdapter final : public ICombatModeAdapter
{
public:
    void Reload(UCombatComponent& C) const override
    {
        if (C.CanStartLegacyReload() && !C.bLocallyReloading)
        {
            C.ServerReload(); C.HandleReload(); C.bLocallyReloading = true;
        }
    }
    void FinishReload(UCombatComponent& C) const override
    {
        if (!C.Character) return;
        if (C.CombatState != ECombatState::ECS_Reloading && !C.bLocallyReloading) return;
        C.bLocallyReloading = false;
        if (C.Character->HasAuthority() && C.CombatState == ECombatState::ECS_Reloading)
        {
            C.CombatState = ECombatState::ECS_Unoccuiped;
            // Per-shell notifies already transferred shotgun ammo; end only unlocks the action.
            if (C.EquippedWeapon && C.EquippedWeapon->GetWeaponType() != EWeaponType::EWT_Shotgun) C.UpdateAmmoValues();
        }
        if (C.bFireButtonPressed) C.Fire();
    }
    void Drop(UCombatComponent& C) const override
    {
        CancelActions(C);
        if (C.EquippedWeapon) { C.EquippedWeapon->Dropped(); C.EquippedWeapon = nullptr; }
    }
    void SetFireHeld(UCombatComponent& C, bool Held) const override
    {
        C.bFireButtonPressed = Held;
        if (Held) C.Fire();
    }
    void CancelFire(UCombatComponent& C) const override { C.ClearLocalFireInput(); }
    void CancelActions(UCombatComponent& C) const override
    {
        C.ClearLocalFireInput(); C.bLocallyReloading = false; C.bSwapAttachPending = false;
        C.CombatState = ECombatState::ECS_Unoccuiped; C.bCanFire = true;
        if (C.Character)
        {
            C.Character->GetWorldTimerManager().ClearTimer(C.FireTimer);
            C.Character->bFinishedSwapping = true;
            C.Character->ForceNetUpdate();
        }
        if (C.SecondaryWeapon) C.SecondaryWeapon->EnableCustomDepth(true);
    }
    bool IsFireHeld(const UCombatComponent& C) const override { return C.bFireButtonPressed; }
    bool CanFire(const UCombatComponent& C) const override
    {
        if (!C.EquippedWeapon || C.EquippedWeapon->IsEmpty() || !C.bCanFire) return false;
        // Legacy shotguns can interrupt per-shell reload; extraction cannot.
        if (C.CombatState == ECombatState::ECS_Reloading && C.EquippedWeapon->GetWeaponType() == EWeaponType::EWT_Shotgun) return true;
        return !C.bLocallyReloading && C.CombatState == ECombatState::ECS_Unoccuiped;
    }
    void StartFireTimer(UCombatComponent& C) const override
    {
        if (!C.Character || !C.EquippedWeapon) return;
        C.bCanFire = false;
        C.Character->GetWorldTimerManager().SetTimer(C.FireTimer, &C, &UCombatComponent::FireTimerFinished, C.EquippedWeapon->FireDelay);
    }
    void SetReloadPrediction(UCombatComponent& C, bool Reloading) const override { C.bLocallyReloading = Reloading; }
    bool IsReloadPredicted(const UCombatComponent& C) const override { return C.bLocallyReloading; }
    ECombatState GetState(const UCombatComponent& C) const override { return C.CombatState; }
    void PredictSwap(UCombatComponent& C) const override
    {
        if (!C.Character || C.Character->HasAuthority() || C.Character->IsDefeated() || C.Character->bDisableGameplay
            || !C.EquippedWeapon || !C.SecondaryWeapon || C.CombatState != ECombatState::ECS_Unoccuiped) return;
        C.Character->PlaySwapMontage();
        C.CombatState = ECombatState::ECS_SwappingWeapon;
        C.Character->bFinishedSwapping = false;
    }
};

class FExtractionCombatModeAdapter final : public ICombatModeAdapter
{
public:
    void Reload(UCombatComponent& C) const override { C.Character->GetExtractionCombat()->RequestReload(); }
    // Animation notifies must never transfer extraction ammo; the server timer owns completion.
    void FinishReload(UCombatComponent&) const override {}
    void Drop(UCombatComponent& C) const override
    {
        if (auto* PC = Cast<AExtractionPlayerController>(C.Character->GetController())) PC->DropEquippedItem();
    }
    void SetFireHeld(UCombatComponent& C, bool Held) const override
    {
        if (C.Character->GetExtractionCombat()->SetFireInputHeld(Held)) C.Fire();
    }
    void CancelFire(UCombatComponent& C) const override { C.Character->GetExtractionCombat()->CancelFireInput(); }
    void CancelActions(UCombatComponent& C) const override
    {
        auto* Extraction = C.Character->GetExtractionCombat();
        Extraction->CancelFireInput(); Extraction->CancelReload(); Extraction->CancelSwap();
    }
    bool IsFireHeld(const UCombatComponent& C) const override { return C.Character->GetExtractionCombat()->IsFireInputHeld(); }
    bool CanFire(const UCombatComponent& C) const override { return C.Character->GetExtractionCombat()->CanFire(); }
    void StartFireTimer(UCombatComponent& C) const override { C.Character->GetExtractionCombat()->StartFireCooldown(); }
    void SetReloadPrediction(UCombatComponent& C, bool Reloading) const override { C.Character->GetExtractionCombat()->SetReloadPrediction(Reloading); }
    bool IsReloadPredicted(const UCombatComponent& C) const override { return C.Character->GetExtractionCombat()->IsReloadPredicted(); }
    ECombatState GetState(const UCombatComponent& C) const override { return C.Character->GetExtractionCombat()->GetState(); }
    // Extraction swap starts on the server and is presented through its multicast.
    void PredictSwap(UCombatComponent&) const override {}
};

const ICombatModeAdapter& UCombatComponent::ModeAdapter() const
{
    static const FLegacyCombatModeAdapter Legacy;
    static const FExtractionCombatModeAdapter Extraction;
    return Character && Character->GetExtractionCombat()->IsEnabled()
        ? static_cast<const ICombatModeAdapter&>(Extraction) : static_cast<const ICombatModeAdapter&>(Legacy);
}

bool UCombatComponent::IsFireInputHeld() const { return ModeAdapter().IsFireHeld(*this); }
void UCombatComponent::CancelFireInput() { ModeAdapter().CancelFire(*this); }
void UCombatComponent::CancelActions() { ModeAdapter().CancelActions(*this); }
void UCombatComponent::ClearLocalFireInput() { bFireButtonPressed = false; }
void UCombatComponent::SetReloadPrediction(bool Reloading) { ModeAdapter().SetReloadPrediction(*this, Reloading); }
bool UCombatComponent::IsReloadPredicted() const { return ModeAdapter().IsReloadPredicted(*this); }
ECombatState UCombatComponent::GetState() const { return ModeAdapter().GetState(*this); }
void UCombatComponent::PredictSwap() { ModeAdapter().PredictSwap(*this); }
