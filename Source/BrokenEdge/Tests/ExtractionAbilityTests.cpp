// 技能施放规则的非 PIE 测试：治疗允许移动施放并降速，救援与烟雾仍需站定。
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Extraction/ExtractionAbilities.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionAbilityMovementTest, "BrokenEdge.Extraction.Abilities.MovementRules",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExtractionAbilityMovementTest::RunTest(const FString&)
{
	const auto* Rules = GetDefault<UExtractionRules>();
	TestTrue(TEXT("Heal slows movement while casting"), Rules->HealMoveSpeedScale > 0.f && Rules->HealMoveSpeedScale < 1.f);
	TestTrue(TEXT("Heal duration is positive"), Rules->HealSeconds > 0.f);

	// 治疗：移动不打断，站定也不打断。
	TestFalse(TEXT("Heal survives movement"),
	          UExtractionAbilities::CancelledByMovement(EExtractionAction::Heal, true));
	TestFalse(TEXT("Heal survives standing still"),
	          UExtractionAbilities::CancelledByMovement(EExtractionAction::Heal, false));
	// 救援与烟雾：移动仍然打断。
	TestTrue(TEXT("Revive is cancelled by movement"),
	         UExtractionAbilities::CancelledByMovement(EExtractionAction::Revive, true));
	TestTrue(TEXT("Smoke is cancelled by movement"),
	         UExtractionAbilities::CancelledByMovement(EExtractionAction::Smoke, true));
	TestFalse(TEXT("Revive survives standing still"),
	          UExtractionAbilities::CancelledByMovement(EExtractionAction::Revive, false));
	TestFalse(TEXT("Smoke survives standing still"),
	          UExtractionAbilities::CancelledByMovement(EExtractionAction::Smoke, false));
	// 无动作时移动不构成取消理由。
	TestFalse(TEXT("No action is never cancelled by movement"),
	          UExtractionAbilities::CancelledByMovement(EExtractionAction::None, true));
	return true;
}
#endif
