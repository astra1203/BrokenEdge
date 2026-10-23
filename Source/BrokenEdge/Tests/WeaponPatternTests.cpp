#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "BrokenEdge/Weapon/WeaponMechanics.h"
#include "BrokenEdge/Extraction/ExtractionItems.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponPatternTest, "BrokenEdge.Extraction.WeaponMechanics.PatternSamples",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponPatternTest::RunTest(const FString&)
{
	// Fixed-distance algorithm samples, not captured gameplay impacts. No mouse correction,
	// sway, recoil recovery, gravity or collision; those are exercised separately in PIE/flight tests.
	FString CSV = TEXT("weapon,stance,aim,mode,shot,right_cm,up_cm,recoil_pitch_deg,recoil_yaw_deg\n");
	for (const FName Id : {FName(TEXT("AK")), FName(TEXT("SCARH"))})
	{
		const auto S = WeaponMechanics::ResolveWeaponStats(ExtractionItems::Make(Id));
		if (!TestTrue(TEXT("Pattern profile valid"), S.IsValid())) return false;
		for (int32 Stance = 0; Stance < 3; ++Stance)
			for (int32 ADS = 0; ADS < 2; ++ADS)
				for (int32 Burst = 0; Burst < 2; ++Burst)
				{
					FVector2D Accum = FVector2D::ZeroVector;
					FVector2D Control = FVector2D::ZeroVector;
					for (uint32 Shot = 0; Shot < 30; ++Shot)
					{
						const uint32 Seed = HashCombine(12345u, Shot);
						FVector2D Spread = (S.Spread[ADS].Base + Accum) * S.SpreadScale;
						if (Stance == 1) Spread *= S.CrouchSpread;
						if (Stance == 2) Spread *= S.MoveSpread;
						const FVector Direction = WeaponMechanics::SpreadDirection(
							FRotator(Control.X, Control.Y, 0).Vector(), Spread, Seed);
						TestTrue(TEXT("Sample intersects forward target plane"), Direction.X > 0);
						const FVector Impact = Direction * (5000. / Direction.X);
						auto Recoil = S.Recoil(Burst ? Shot : 0, Burst && Shot > 0, ADS, Seed);
						if (Stance == 1) Recoil *= S.CrouchRecoil;
						CSV += FString::Printf(TEXT("%s,%s,%s,%s,%u,%.6f,%.6f,%.6f,%.6f\n"),
						                       *Id.ToString(),
						                       Stance == 0
							                       ? TEXT("stand")
							                       : Stance == 1
							                       ? TEXT("crouch")
							                       : TEXT("move"),
						                       ADS ? TEXT("ADS") : TEXT("hip"), Burst ? TEXT("burst") : TEXT("single"),
						                       Shot + 1, Impact.Y, Impact.Z, Recoil.X, Recoil.Y);
						if (Burst)
						{
							Control += Recoil;
							const auto& P = S.Spread[ADS];
							Accum.X = FMath::Max(
								0., FMath::Min(Accum.X + P.PerShot.X, P.Maximum.X) - P.Recovery.X * S.Interval);
							Accum.Y = FMath::Max(
								0., FMath::Min(Accum.Y + P.PerShot.Y, P.Maximum.Y) - P.Recovery.Y * S.Interval);
						}
					}
				}
	}
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Extraction/WeaponPatterns");
	IFileManager::Get().MakeDirectory(*Directory, true);
	TestTrue(TEXT("Export reproducible 50m pattern CSV"), FFileHelper::SaveStringToFile(
		         CSV, *(Directory / TEXT("Patterns.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	return true;
}
#endif
