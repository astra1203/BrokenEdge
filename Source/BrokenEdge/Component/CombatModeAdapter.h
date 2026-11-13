#pragma once
#include "BrokenEdge/BrokenEdgeTypes/CombatState.h"

class UCombatComponent;

// Stateless dispatch; each mode keeps input and protocol state in its owning component.
class ICombatModeAdapter
{
public:
    virtual ~ICombatModeAdapter() = default;
    virtual void Reload(UCombatComponent& Combat) const = 0;
    virtual void FinishReload(UCombatComponent& Combat) const = 0;
    virtual void Drop(UCombatComponent& Combat) const = 0;
    virtual void SetFireHeld(UCombatComponent& Combat, bool Held) const = 0;
    virtual void CancelFire(UCombatComponent& Combat) const = 0;
    virtual bool IsFireHeld(const UCombatComponent& Combat) const = 0;
    virtual bool CanFire(const UCombatComponent& Combat) const = 0;
    virtual void StartFireTimer(UCombatComponent& Combat) const = 0;
    virtual void SetReloadPrediction(UCombatComponent& Combat, bool Reloading) const = 0;
    virtual bool IsReloadPredicted(const UCombatComponent& Combat) const = 0;
    virtual ECombatState GetState(const UCombatComponent& Combat) const = 0;
    virtual void PredictSwap(UCombatComponent& Combat) const = 0;
};
