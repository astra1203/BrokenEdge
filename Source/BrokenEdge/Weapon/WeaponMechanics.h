#pragma once

#include "CoreMinimal.h"

struct FExtractionItem;

/** Source units are kept in the snapshot; this runtime boundary uses cm, seconds and degrees.
 * Unverified DFM angle composition and falloff are explicitly compatibility policies, not official formulas. */
struct BROKENEDGE_API FWeaponRecoilSequence
{
	TArray<float> Pitch, Yaw;
	FVector2D PitchRandom = FVector2D::ZeroVector, YawRandom = FVector2D::ZeroVector;
	float PitchScale = 1, YawScale = 1;
};

struct BROKENEDGE_API FWeaponSpreadProfile
{
	FVector2D Base = FVector2D::ZeroVector;
	FVector2D Maximum = FVector2D::ZeroVector;
	FVector2D PerShot = FVector2D::ZeroVector;
	FVector2D Recovery = FVector2D::ZeroVector;
};

struct BROKENEDGE_API FResolvedWeaponStats
{
	FName Definition;
	uint32 Revision = 0;
	float MagazineCapacity = 30;
	float Interval = .1f, AimIn = .3f, AimOut = .3f, SprintToFire = .2f;
	float Reload = 2, EmptyReload = 2.5f, Equip = .4f, Unequip = .4f;
	float Speed = 52500, Gravity = 980, GravityFreeDistance = 2500, MaxDistance = 100000, Radius = 1;
	float RangeStart = 4000;
	TArray<float> FalloffDistances, FalloffRates;
	FWeaponRecoilSequence Single[2], Continuous[2], Loop[2];
	FWeaponSpreadProfile Spread[2];
	float StandBreath = 1, CrouchBreath = .5f, BreathDrain = 5, BreathShot = 4, BreathRecovery = 17, BreathCooldown = 1;
	float RecoilScale = 1, SpreadScale = 1;
	float MoveSpread = 1, CrouchSpread = 1, JumpSpread = 1;
	FVector2D CrouchRecoil = FVector2D::UnitVector;
	// Deliberately explicit compatibility tuning until DFM angular units are verified.
	float SourceRecoilToDegrees = .1f;
	TArray<FString> Sources;
	bool IsValid() const { return Revision != 0; }
	float Falloff(float Distance) const;
	FVector2D Recoil(uint32 Sequence, bool bContinuous, float ADS, uint32 Seed) const;
};

namespace WeaponMechanics
{
	BROKENEDGE_API const FResolvedWeaponStats* Find(FName Definition);
	BROKENEDGE_API FResolvedWeaponStats ResolveWeaponStats(const FExtractionItem& Item);
	BROKENEDGE_API FVector SpreadDirection(const FVector& Direction, FVector2D Degrees, uint32 Seed);
}
