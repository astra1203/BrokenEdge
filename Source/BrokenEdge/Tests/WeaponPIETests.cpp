#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Extraction/ExtractionCombatComponent.h"
#include "BrokenEdge/Extraction/ExtractionPlayerController.h"
#include "BrokenEdge/Extraction/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/ExtractionInteractionComponent.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "BrokenEdge/Weapon/WeaponPresentation.h"
#include "BrokenEdge/Extraction/ExtractionMeleeComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "BrokenEdge/Character/StingerFirstPersonAnimInstance.h"

class FWeaponSprintPIESequence : public IAutomationLatentCommand
{
public:
	explicit FWeaponSprintPIESequence(FAutomationTestBase* InTest) : Test(InTest)
	{
	}

	virtual bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 60)
		{
			Test->AddError(FString::Printf(TEXT("Sprint PIE timed out: weapon %d step %d"), Weapon, Step));
			return true;
		}
		auto* World = GEditor ? GEditor->PlayWorld.Get() : nullptr;
		auto* PC = World ? World->GetFirstPlayerController() : nullptr;
		auto* C = PC ? Cast<ABrokenEdgeCharacter>(PC->GetPawn()) : nullptr;
		if (!C || !C->GetEquippedWeapon()) return false;
		auto* M = C->GetExtractionCombat();
		const double Now = World->GetTimeSeconds();
		auto Key = [&](FKey K, bool Down)
		{
			PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, Down ? IE_Pressed : IE_Released, Down ? 1.f : 0.f));
		};
		auto Next = [&]
		{
			++Step;
			StepAt = Now;
		};
		if (Step == 0 && M->CanAct() && M->CanFireMechanics())
		{
			if (Weapon == 1)
			{
				const auto* Mesh = C->GetFirstPersonMesh();
				const auto* Anim = Mesh ? Cast<UStingerFirstPersonAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
				Test->TestTrue(TEXT("SCAR preserves its own idle animation"), Anim
				               && C->GetEquippedWeapon()->Presentation && Anim->ActiveAnimation == C->
				               GetEquippedWeapon()->Presentation->Idle);
			}
			Origin = C->GetActorLocation();
			Key(EKeys::W, true);
			Key(EKeys::LeftShift, true);
			Next();
		}
		else if (Step == 1 && Now - StepAt > .8)
		{
			Test->TestTrue(TEXT("Forward + Shift enters sprint"), C->IsSprinting());
			const auto* Mesh = C->GetFirstPersonMesh();
			const auto* Anim = Mesh ? Cast<UStingerFirstPersonAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
			Test->TestTrue(TEXT("Original first-person sprint animation is active"), Anim
			               && Anim->CurrentVisualState == TEXT("Sprint") && Anim->bLocomotionMotion && !Anim->
			               bMissingStateAnimation);
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / FString::Printf(
				                                      TEXT("Extraction/SprintPIE-%d.png"), Weapon), true, false);
			Shots = M->GetAcceptedShots();
			Key(EKeys::LeftMouseButton, true);
			Next();
		}
		else if (Step == 2)
		{
			Test->TestFalse(TEXT("Fire interrupts sprint with Shift held"), C->IsSprinting());
			if (Now - StepAt < M->GetWeaponStats().SprintToFire)
				Test->TestEqual(TEXT("Sprint delay prevents an early shot"), M->GetAcceptedShots(), Shots);
			if (M->GetAcceptedShots() > Shots)
			{
				Test->TestTrue(
					TEXT("First shot has bounded sprint delay"), Now - StepAt < M->GetWeaponStats().SprintToFire + .25);
				Next();
			}
		}
		else if (Step == 3 && Now - StepAt > .25)
		{
			Test->TestFalse(TEXT("Held fire suppresses resprint"), C->IsSprinting());
			Key(EKeys::LeftMouseButton, false);
			Next();
		}
		else if (Step == 4 && Now - StepAt > .3)
		{
			Test->TestTrue(TEXT("Release fire resumes held Shift sprint"), C->IsSprinting());
			Shots = M->GetAcceptedShots();
			Key(EKeys::LeftMouseButton, true);
			Next();
		}
		else if (Step == 5)
		{
			Key(EKeys::LeftMouseButton, false);
			Next();
		}
		else if (Step == 6 && Now - StepAt > .4)
		{
			Test->TestEqual(TEXT("Released short tap does not fire later"), M->GetAcceptedShots(), Shots);
			Key(EKeys::W, false);
			Key(EKeys::LeftShift, false);
			if (Weapon == 1) return true;
			C->SetActorLocation(Origin);
			Key(EKeys::Two, true);
			Next();
		}
		else if (Step == 7 && Now - StepAt > 3)
		{
			Key(EKeys::Two, false);
			Test->TestEqual(TEXT("SCAR selected for second sprint test"), C->GetEquippedWeapon()->ExtractionDefinition,
			                FName(TEXT("SCARH")));
			Weapon = 1;
			Step = 0;
		}
		return false;
	}

private:
	FAutomationTestBase* Test;
	double Started = FPlatformTime::Seconds(), StepAt = 0;
	int32 Step = 0, Weapon = 0;
	uint32 Shots = 0;
	FVector Origin;
};

class FWeaponPIESequence : public IAutomationLatentCommand
{
public:
	explicit FWeaponPIESequence(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()),
	                                                           StepAt(Started)
	{
	}

	virtual bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - Started > 120)
		{
			Test->AddError(FString::Printf(TEXT("PIE weapon sequence timed out at step %d"), Step));
			return true;
		}
		UWorld* W = GEditor ? GEditor->PlayWorld.Get() : nullptr;
		auto* PC = W ? Cast<AExtractionPlayerController>(W->GetFirstPlayerController()) : nullptr;
		auto* C = PC ? Cast<ABrokenEdgeCharacter>(PC->GetPawn()) : nullptr;
		if (!C || !C->GetEquippedWeapon() || !C->GetExtractionCombat()->GetWeaponStats().IsValid()) return false;
		auto* Mechanics = C->GetExtractionCombat();
		auto* Input = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
		auto* Toggle = LoadObject<UInputAction>(
			nullptr, TEXT("/Game/Input/Actions/IA_ToggleCameraView.IA_ToggleCameraView"));
		Input->InjectInputForAction(Toggle, FInputActionValue(false), {}, {});
		if (Step == 2 && Mechanics->GetBreathResumeTime() > W->GetTimeSeconds()) SawExhaustion = true;
		auto Key = [&](FKey K, bool Down)
		{
			PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, Down ? IE_Pressed : IE_Released, Down ? 1.f : 0.f));
		};
		auto Next = [&]
		{
			++Step;
			StepAt = Now;
		};
		if (Step == 0 && Mechanics->CanAct() && Mechanics->CanFireMechanics())
		{
			Test->TestTrue(TEXT("Real Play In Editor world"), W->WorldType == EWorldType::PIE);
			Key(EKeys::RightMouseButton, true);
			Next();
		}
		else if (Step == 1 && Now - StepAt > 1)
		{
			Test->TestTrue(TEXT("PIE input finishes ADS"), Mechanics->GetAimAlpha() > .99f);
			Breath = Mechanics->GetBreath();
			Key(EKeys::LeftAlt, true);
			Next();
		}
		else if (Step == 2 && Now - StepAt > 22)
		{
			Test->TestTrue(TEXT("PIE input consumes breath"), Mechanics->GetBreath() < Breath);
			Test->TestTrue(TEXT("PIE held breath enters exhaustion cooldown"), SawExhaustion);
			Key(EKeys::LeftAlt, false);
			Key(EKeys::RightMouseButton, false);
			Shots = Mechanics->GetAcceptedShots();
			Key(EKeys::LeftMouseButton, true);
			Next();
		}
		else if (Step == 3 && Now - StepAt > .06)
		{
			Key(EKeys::LeftMouseButton, false);
			Next();
		}
		else if (Step == 4 && Now - StepAt > .5)
		{
			Test->TestTrue(TEXT("PIE physical firing accepted"), Mechanics->GetAcceptedShots() > Shots);
			Test->TestTrue(
				TEXT("PIE ammunition consumed"),
				C->GetEquippedWeapon()->GetAmmo() < C->GetEquippedWeapon()->GetMagCapacity());
			Key(EKeys::R, true);
			Next();
		}
		else if (Step == 5 && Now - StepAt > 4)
		{
			Key(EKeys::R, false);
			Test->TestTrue(TEXT("PIE released breath recovers"), Mechanics->GetBreath() > 1.f);
			Test->TestEqual(TEXT("PIE reload replenishes magazine"), C->GetEquippedWeapon()->GetAmmo(),
			                C->GetEquippedWeapon()->GetMagCapacity());
			Key(EKeys::Two, true);
			Next();
		}
		else if (Step == 6 && Now - StepAt > 3)
		{
			Key(EKeys::Two, false);
			Test->TestEqual(TEXT("PIE secondary SCAR selected"), C->GetEquippedWeapon()->ExtractionDefinition,
			                FName(TEXT("SCARH")));
			PC->RequestOpenInventory(nullptr, true);
			Next();
		}
		else if (Step == 7 && Now - StepAt > .5)
		{
			auto* Inventory = C->GetExtractionInteraction()->Inventory();
			Test->TestTrue(TEXT("PIE inventory opened"), C->GetExtractionInteraction()->InventoryOpen);
			const auto* Gun = Inventory->Contents.Items.FindByPredicate([](const auto& I)
			{
				return I.Definition == TEXT("AK");
			});
			const auto* Grip = Inventory->Contents.Items.FindByPredicate([](const auto& I)
			{
				return I.Definition == TEXT("VerticalGrip");
			});
			if (!Gun || !Grip)
			{
				Test->AddError(TEXT("PIE training supply missing"));
				return true;
			}
			GunId = Gun->Id;
			GripId = Grip->Id;
			FExtractionItemRequest R;
			R.Op = EExtractionItemOp::Attach;
			R.Item = GripId;
			R.Weapon = GunId;
			PC->RequestItem(R);
			Next();
		}
		else if (Step == 8 && Now - StepAt > .5)
		{
			const auto* Gun = C->GetExtractionInteraction()->Inventory()->Contents.Find(GunId);
			Test->TestTrue(TEXT("PIE attachment production transaction"), Gun && Gun->Attachments.Num() == 1);
			const auto* Def = UExtractionItemDefinition::Get(TEXT("VerticalGrip"));
			FExtractionItemRequest R;
			R.Op = EExtractionItemOp::Tune;
			R.Item = GunId;
			R.MountedAttachment = GripId;
			R.TuneId = Def->Tunes[0].Id;
			R.TuneValue = Def->Tunes[0].Maximum;
			PC->RequestItem(R);
			Next();
		}
		else if (Step == 9 && Now - StepAt > .5)
		{
			const auto* Gun = C->GetExtractionInteraction()->Inventory()->Contents.Find(GunId);
			Test->TestTrue(
				TEXT("PIE tuning reaches runtime resolver"),
				Gun && WeaponMechanics::ResolveWeaponStats(*Gun).RecoilScale < 1.f);
			PC->RequestOpenInventory(nullptr, false);
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Extraction/WeaponRangePIE.png"),
			                                      true, false);
			Next();
		}
		else if (Step == 10 && Now - StepAt > 2)
		{
			Key(EKeys::Three, true);
			Next();
		}
		else if (Step == 11 && Now - StepAt > .3)
		{
			Key(EKeys::Three, false);
			Test->TestTrue(TEXT("PIE knife selected by input"), C->Melee->Active);
			Next();
		}
		else if (Step == 12 && Now - StepAt > .1)
		{
			Test->TestEqual(
				TEXT("PIE knife parent follows recreated arms"), C->Melee->IsViewAttached(), C->IsFirstPersonView());
			StepAt = Now;
			if (KnifeSwitches++ == 30) Next();
			else Input->InjectInputForAction(Toggle, FInputActionValue(true), {}, {});
		}
		else if (Step == 13)
		{
			Key(EKeys::RightMouseButton, true);
			Next();
		}
		else if (Step == 14 && Now - StepAt > .1)
		{
			Key(EKeys::RightMouseButton, false);
			Input->InjectInputForAction(Toggle, FInputActionValue(true), {}, {});
			Next();
		}
		else if (Step == 15 && Now - StepAt > .1)
		{
			Test->TestFalse(TEXT("PIE attack switched to third person"), C->IsFirstPersonView());
			Input->InjectInputForAction(Toggle, FInputActionValue(true), {}, {});
			Next();
		}
		else if (Step == 16 && Now - StepAt > .1)
		{
			Test->TestTrue(TEXT("PIE knife attack remains attached after view recreation"), C->Melee->IsViewAttached());
			Test->TestTrue(TEXT("PIE recreated arms resume heavy attack"), C->GetFirstPersonMesh()
			               && C->GetFirstPersonMesh()->GetAnimInstance()->Montage_IsPlaying(C->Melee->HeavyView));
			Next();
		}
		else if (Step == 17 && Now - StepAt > 1.5)
		{
			Key(EKeys::One, true);
			Next();
		}
		else if (Step == 18 && Now - StepAt > .3)
		{
			Key(EKeys::One, false);
			Test->TestFalse(TEXT("PIE returns from knife to firearm"), C->Melee->Active);
			return true;
		}
		return false;
	}

private:
	FAutomationTestBase* Test;
	double Started, StepAt;
	int32 Step = 0;
	int32 KnifeSwitches = 0;
	float Breath = 0;
	bool SawExhaustion = false;
	uint32 Shots = 0;
	FGuid GunId, GripId;
};

class FRestoreWeaponPIESettings : public IAutomationLatentCommand
{
public:
	FRestoreWeaponPIESettings()
	{
		const auto* Settings = GetDefault<ULevelEditorPlaySettings>();
		Settings->GetPlayNetMode(Mode);
		Settings->GetPlayNumberOfClients(Clients);
		Settings->GetRunUnderOneProcess(OneProcess);
		SeparateServer = Settings->bLaunchSeparateServer;
	}

	virtual bool Update() override
	{
		auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
		Settings->SetPlayNetMode(Mode);
		Settings->SetPlayNumberOfClients(Clients);
		Settings->SetRunUnderOneProcess(OneProcess);
		Settings->bLaunchSeparateServer = SeparateServer;
		return true;
	}

private:
	EPlayNetMode Mode = PIE_Standalone;
	int32 Clients = 1;
	bool OneProcess = true;
	bool SeparateServer = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponPIESmokeTest, "BrokenEdge.Extraction.WeaponPIE.InputAndAssembly",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponPIESmokeTest::RunTest(const FString&)
{
	// Do not inherit a user's multi-client PIE setup for this local input smoke test.
	const auto Restore = MakeShared<FRestoreWeaponPIESettings>();
	auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_Standalone);
	Settings->SetPlayNumberOfClients(1);
	Settings->SetRunUnderOneProcess(true);
	Settings->bLaunchSeparateServer = false;
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Extraction/Training/L_WeaponRange")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FWeaponPIESequence>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponSprintPIETest, "BrokenEdge.Extraction.WeaponPIE.SprintTransitions",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponSprintPIETest::RunTest(const FString&)
{
	const auto Restore = MakeShared<FRestoreWeaponPIESettings>();
	auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_Standalone);
	Settings->SetPlayNumberOfClients(1);
	Settings->SetRunUnderOneProcess(true);
	Settings->bLaunchSeparateServer = false;
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Extraction/Training/L_WeaponRange")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FWeaponSprintPIESequence>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}
#endif
