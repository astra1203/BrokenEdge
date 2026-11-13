#include "CombatNetworkTestMode.h"
#include "BrokenEdge/Extraction/ExtractionState.h"

void ACombatNetworkTestMode::HandleStartingNewPlayer_Implementation(APlayerController* Player)
{
    auto* GS = GetGameState<AExtractionGameState>();
    if (!GS) return;
    // Training ticks Active before remote clients finish connecting. Scope the
    // normal spawn/loadout path to Waiting without changing real raid admission.
    TGuardValue<EExtractionPhase> RestorePhase(GS->Phase, EExtractionPhase::Waiting);
    Super::HandleStartingNewPlayer_Implementation(Player);
}
