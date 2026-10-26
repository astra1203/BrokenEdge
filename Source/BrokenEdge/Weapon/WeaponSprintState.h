#pragma once

#include "CoreMinimal.h"

/** Local prediction and authority each advance this state using their own world clock. */
struct FWeaponSprintState
{
	bool FireHeld = false;
	bool PendingShot = false;
	double FireReadyAt = 0;
	double SprintResumeAt = 0;

	void EndSprint(double Now, float Delay)
	{
		FireReadyAt = FMath::Max(FireReadyAt, Now + FMath::Max(0.f, Delay));
	}

	void SetFireHeld(bool Held, double Now, float ReturnInterval)
	{
		if (Held && !FireHeld) PendingShot = true;
		if (!Held)
		{
			PendingShot = false;
			if (FireHeld) SprintResumeAt = Now + FMath::Max(0.f, ReturnInterval);
		}
		FireHeld = Held;
	}

	bool CanSprint(double Now) const { return !FireHeld && Now >= SprintResumeAt; }
	bool CanFire(double Now) const { return Now >= FireReadyAt; }
};
