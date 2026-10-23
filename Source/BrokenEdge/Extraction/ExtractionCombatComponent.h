#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BrokenEdge/Weapon/WeaponMechanics.h"
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
	void FireGuard(const FVector& Target);
	void RequestReload();
	void BeginServerReload();
	void CancelReload();
	void BeginServerSwap();
	void CancelSwap();
	void EquipmentAmmo(AWeapon* Weapon, int32 Ammo);
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void ServerRequestShot(uint32 ShotId, AWeapon* Weapon, FVector_NetQuantize Target);
	UFUNCTION(Server, Reliable)
	void ServerRequestShotVersioned(uint32 ShotId, AWeapon* Weapon, FVector_NetQuantize Target, uint32 Revision);
	UFUNCTION(Server, Reliable)
	void ServerDropWeapon();
	uint32 GetAcceptedShots() const { return AcceptedShots; }
	uint32 GetRejectedShots() const { return RejectedShots; }
	virtual void TickComponent(float Delta, ELevelTick Kind, FActorComponentTickFunction* Tick) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool CanFireMechanics() const;
	const FResolvedWeaponStats& GetWeaponStats() const { return Stats; }
	float GetAimAlpha() const { return AimAlpha; }
	float GetBreath() const { return Breath; }
	double GetBreathResumeTime() const { return BreathResume; }
	FRotator GetVisualKick() const { return FRotator(-VisualKick, 0, 0); }
	void HoldBreathPressed();
	void HoldBreathReleased();
	UFUNCTION(Server, Reliable)
	void ServerHoldBreath(bool bHeld);

private:
	ABrokenEdgeCharacter* Character() const;
	FResolvedWeaponStats Stats;
	TWeakObjectPtr<AWeapon> MechanicsWeapon;
	TArray<FName> MechanicsParts;
	uint32 Configuration = 0;
	FVector2D SpreadAccumulation = FVector2D::ZeroVector;
	FVector2D RecoilDebt = FVector2D::ZeroVector;
	FRotator PreviousControl = FRotator::ZeroRotator;
	bool bHaveControl = false;
	float AimAlpha = 0, Breath = 100, BreathResume = 0, VisualKick = 0;
	double LastMechanicsShot = -100, ReadyAt = 0;
	uint32 BurstIndex = 0;
	bool bBreathHeld = false;
	void ResetTransientMechanics();
	bool CanSteadyWeapon() const;
	UPROPERTY(Replicated)
	uint32 MechanicsSeed = 0;
	void RefreshMechanics();
	FVector ResolveShot(const FVector& Target, uint32 Id, bool bApplyInput);
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
	UFUNCTION(Client, Reliable)
	void ClientMechanicsResult(uint32 ShotId, AWeapon* Weapon, uint32 Sequence, FVector2D Spread, float InBreath,
	                           double ServerTime, double ShotTime);
	UFUNCTION(NetMulticast, Reliable)
	void MulticastShotEffects(AWeapon* Weapon, FVector_NetQuantize Target, int32 Ammo);
	UFUNCTION(NetMulticast, Reliable)
	void MulticastReloadAmmo(AWeapon* Weapon, int32 Ammo);
	UFUNCTION(Client, Reliable)
	void ClientReloadRejected();
};
