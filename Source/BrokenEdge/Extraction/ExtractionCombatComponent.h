#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ExtractionCombatComponent.generated.h"

class AWeapon;
class ABrokenEdgeCharacter;

/** Mode-scoped authority and owner prediction. Legacy weapons keep their existing protocol. */
UCLASS()
class BROKENEDGE_API UExtractionCombatComponent : public UActorComponent
{
	GENERATED_BODY()
	friend class FExtractionDriver;

public:
	UExtractionCombatComponent();
	bool IsEnabled() const;
	bool CanAct() const;
	void RequestShot(const FVector& Target);
	void RequestReload();
	void BeginServerReload();
	void CancelReload();
	void BeginServerSwap();
	void CancelSwap();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UFUNCTION(Server, Reliable)
	void ServerRequestShot(uint32 ShotId, AWeapon* Weapon, FVector_NetQuantize Target);
	UFUNCTION(Server, Reliable)
	void ServerDropWeapon();
	uint32 GetAcceptedShots() const { return AcceptedShots; }
	uint32 GetRejectedShots() const { return RejectedShots; }

private:
	ABrokenEdgeCharacter* Character() const;
	uint32 LocalShotId = 0;
	uint32 LastProcessedShot = 0;
	uint32 LastAcknowledgedShot = 0;
	uint32 AcceptedShots = 0;
	uint32 RejectedShots = 0;
	double NextShotTime = 0;
	TMap<uint32, TWeakObjectPtr<AWeapon>> PendingShots;
	TWeakObjectPtr<AWeapon> ReloadWeapon;
	FTimerHandle ReloadTimer;
	FTimerHandle SwapAttachTimer, SwapFinishTimer;
	TWeakObjectPtr<AWeapon> SwapPrimary, SwapSecondary;
	void CommitServerSwap();
	void FinishServerSwap();
	void FinishServerReload();
	UFUNCTION(NetMulticast, Reliable)
	void MulticastSwapStarted();
	UFUNCTION(Client, Reliable)
	void ClientShotResult(uint32 ShotId, AWeapon* Weapon, int32 Ammo, bool bAccepted);
	UFUNCTION(NetMulticast, Reliable)
	void MulticastShotEffects(AWeapon* Weapon, FVector_NetQuantize Target, int32 Ammo);
	UFUNCTION(NetMulticast, Reliable)
	void MulticastReloadAmmo(AWeapon* Weapon, int32 Ammo);
	UFUNCTION(Client, Reliable)
	void ClientReloadRejected();
};
