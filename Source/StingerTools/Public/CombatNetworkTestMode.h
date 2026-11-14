#pragma once
#include "BrokenEdge/Extraction/Core/WeaponRangeGameMode.h"
#include "CombatNetworkTestMode.generated.h"

/** Editor-only network training fixture; never used by production map assets. */
UCLASS(NotBlueprintable, Transient)
class ACombatNetworkTestMode : public AWeaponRangeGameMode
{
    GENERATED_BODY()
public:
    virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& Id, FString& Error) override
    { AGameMode::PreLogin(Options, Address, Id, Error); }
    virtual void HandleStartingNewPlayer_Implementation(APlayerController* Player) override;
};
