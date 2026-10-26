#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Weapon/WeaponSprintState.h"
#include "BrokenEdge/Weapon/WeaponMechanics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponSprintTimingTest, "BrokenEdge.Extraction.WeaponMechanics.SprintTransitions",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponSprintTimingTest::RunTest(const FString&)
{
	for (FName Id : {FName(TEXT("AK")), FName(TEXT("SCARH"))})
	{
		const auto* Stats = WeaponMechanics::Find(Id);
		if (!TestNotNull(TEXT("Weapon source profile"), Stats)) return false;
		TestEqual(TEXT("Source sprint return interval"), Stats->ReturnSprintInterval, .1f);
		FWeaponSprintState State;
		State.SetFireHeld(true, 10., Stats->ReturnSprintInterval);
		TestFalse(TEXT("Fire input interrupts sprint"), State.CanSprint(10.));
		State.EndSprint(10., Stats->SprintToFire);
		const double Deadline = State.FireReadyAt;
		TestFalse(TEXT("Cannot fire before weapon delay"), State.CanFire(Deadline - .001));
		TestTrue(TEXT("First shot becomes ready without releasing Shift"), State.CanFire(Deadline));
		State.SetFireHeld(true, Deadline + .5, Stats->ReturnSprintInterval);
		TestEqual(TEXT("Held input does not push readiness forward"), State.FireReadyAt, Deadline);
		TestFalse(TEXT("Holding fire suppresses sprint after delay"), State.CanSprint(Deadline + 1.));
		State.SetFireHeld(false, 12., Stats->ReturnSprintInterval);
		TestFalse(TEXT("Release cancels queued shot"), State.PendingShot);
		TestFalse(TEXT("Release respects return interval"), State.CanSprint(12.));
		TestTrue(TEXT("Held Shift can resume after interval"), State.CanSprint(State.SprintResumeAt));
		State.SetFireHeld(true, 13., Stats->ReturnSprintInterval);
		State.EndSprint(13., Stats->SprintToFire);
		State.SetFireHeld(false, 13.01, Stats->ReturnSprintInterval);
		TestFalse(TEXT("Short tap never leaves a queued shot"), State.PendingShot);
	}
	FWeaponSprintState Walking;
	Walking.SetFireHeld(true, 1., .1f);
	TestTrue(TEXT("Normal movement does not incur sprint delay"), Walking.CanFire(1.));
	return true;
}
#endif
