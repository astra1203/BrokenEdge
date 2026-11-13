#include "CombatModeAdapter.h"
#include "CombatComponent.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Extraction/ExtractionCombatComponent.h"
#include "BrokenEdge/Extraction/ExtractionPlayerController.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "TimerManager.h"

class FLegacyCombatModeAdapter final : public ICombatModeAdapter
{
public:
    void Reload(UCombatComponent& C) const override
    {
        if (C.CarriedAmmo > 0 && C.CombatState == ECombatState::ECS_Unoccuiped && C.EquippedWeapon
            && !C.EquippedWeapon->IsFull() && !C.bLocallyReloading)
        {
            C.ServerReload(); C.HandleReload(); C.bLocallyReloading = true;
        }
    }
    void FinishReload(UCombatComponent& C) const override
    {
        if (!C.Character) return;
        C.bLocallyReloading = false;
        if (C.Character->HasAuthority())
        {
            C.CombatState = ECombatState::ECS_Unoccuiped;
            C.UpdateAmmoValues();
        }
        if (C.bFireButtonPressed) C.Fire();
    }
    void Drop(UCombatComponent& C) const override
    {
        if (C.EquippedWeapon) { C.EquippedWeapon->Dropped(); C.EquippedWeapon = nullptr; }
    }
    void SetFireHeld(UCombatComponent& C, bool Held) const override
    {
        C.bFireButtonPressed = Held;
        if (Held) C.Fire();
    }
    void CancelFire(UCombatComponent& C) const override { C.ClearLocalFireInput(); }
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
    bool IsFireHeld(const UCombatComponent& C) const override { return C.Character->GetExtractionCombat()->IsFireInputHeld(); }
    bool CanFire(const UCombatComponent& C) const override { return C.Character->GetExtractionCombat()->CanFire(); }
    void StartFireTimer(UCombatComponent& C) const override { C.Character->GetExtractionCombat()->StartFireCooldown(); }
    void SetReloadPrediction(UCombatComponent& C, bool Reloading) const override { C.Character->GetExtractionCombat()->SetReloadPrediction(Reloading); }
    bool IsReloadPredicted(const UCombatComponent& C) const override { return C.Character->GetExtractionCombat()->IsReloadPredicted(); }
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
void UCombatComponent::ClearLocalFireInput() { bFireButtonPressed = false; }
void UCombatComponent::SetReloadPrediction(bool Reloading) { ModeAdapter().SetReloadPrediction(*this, Reloading); }
bool UCombatComponent::IsReloadPredicted() const { return ModeAdapter().IsReloadPredicted(*this); }
