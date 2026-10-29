#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerStart.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/StaticMeshComponent.h"
#include "BrokenEdge/Extraction/ExtractionWidget.h"
#include "BrokenEdge/Extraction/ExtractionPlayerController.h"
#include "BrokenEdge/Extraction/ExtractionLootActor.h"
#include "BrokenEdge/Extraction/ExtractionGameMode.h"

namespace
{
	class FCheckExtractionBlueprints : public IAutomationLatentCommand
	{
	public:
		FCheckExtractionBlueprints(FAutomationTestBase* InTest, int32 InClients) : Test(InTest), Clients(InClients)
		{
		}

		bool Update() override
		{
			if (CapturedAt > 0) return FPlatformTime::Seconds() - CapturedAt > 1.0;
			if (Start == 0) Start = FPlatformTime::Seconds();
			TArray<UWorld*> Worlds;
			int32 LocalPlayers = 0;
			for (const auto& Context : GEngine->GetWorldContexts())
				if (Context.WorldType == EWorldType::PIE && Context.World())
				{
					Worlds.Add(Context.World());
					for (TActorIterator<AExtractionPlayerController> It(Context.World()); It; ++It)
						if (It->IsLocalController() && It->GetLocalPlayer()) ++LocalPlayers;
				}
			if (LocalPlayers < Clients || FPlatformTime::Seconds() - Start < 3)
			{
				if (FPlatformTime::Seconds() - Start < 60) return false;
				Test->AddError(TEXT("PIE local controllers did not initialize"));
				return true;
			}
			for (auto* World : Worlds)
			{
				TArray<UUserWidget*> Widgets;
				UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, UExtractionWidget::StaticClass(), false);
				for (TActorIterator<AExtractionPlayerController> It(World); It; ++It)
				{
					int32 Count = 0;
					for (auto* W : Widgets)
						if (W->GetOwningPlayer() == *It && W->IsInViewport())
						{
							++Count;
							// Exercise retained-controller reattachment without claiming a full map-travel test.
							W->RemoveFromParent();
							It->PostSeamlessTravel();
							Test->TestTrue(TEXT("Travel hook reattaches the existing HUD"), W->IsInViewport());
							Test->TestEqual(
								TEXT("HUD uses configured blueprint"), W->GetClass(), It->ExtractionWidgetClass.Get());
							for (const TCHAR* Name : {
								     TEXT("Status"), TEXT("SmokeHaze"), TEXT("Vitals"), TEXT("WeaponStatus"),
								     TEXT("ExitStatus"), TEXT("Compass"), TEXT("ExtractionCountdown"),
								     TEXT("ExtractionProgress"), TEXT("HealthProgress"), TEXT("Previous"), TEXT("Next"),
								     TEXT("Leave")
							     })
								Test->TestNotNull(Name, W->WidgetTree->FindWidget(FName(Name)));
							for (const TCHAR* Name : {TEXT("Previous"), TEXT("Next"), TEXT("Leave")})
							{
								auto* Button = Cast<UButton>(W->WidgetTree->FindWidget(FName(Name)));
								Test->TestTrue(TEXT("Button delegate bound"), Button && Button->OnClicked.IsBound());
							}
						}
					Test->TestEqual(
						TEXT("Exactly one HUD per local controller, none for remote controllers"), Count,
						It->IsLocalController() && It->GetLocalPlayer() ? 1 : 0);
				}
				if (World->GetAuthGameMode<AExtractionGameMode>())
				{
					for (bool Loose : {false, true})
					{
						auto* Loot = AExtractionLootActor::SpawnContainer(
							World, FVector(0, 0, 10000), Loose, TEXT("Ammo"));
						if (!Test->TestNotNull(TEXT("Runtime loot created"), Loot)) continue;
						Test->TestEqual(
							TEXT("Loot uses configured blueprint"), Loot->GetClass(),
							World->GetAuthGameMode<AExtractionGameMode>()->LootActorClass.Get());
						Test->TestFalse(TEXT("Runtime loot does not seed extra inventory"), Loot->SeedContents);
						Test->TestTrue(TEXT("Runtime loot initially empty"), Loot->Contents.Items.IsEmpty());
						Test->TestTrue(TEXT("Loot visual mesh"), Loot->Mesh->GetStaticMesh() != nullptr);
						const FTransform Visual = Loot->Mesh->GetRelativeTransform();
						Loot->OnConstruction(Loot->GetActorTransform());
						Test->TestTrue(
							TEXT("Appearance refresh is idempotent"),
							Visual.Equals(Loot->Mesh->GetRelativeTransform()));
						Loot->Destroy();
					}
				}
			}
			if (Clients == 1 && !FParse::Param(FCommandLine::Get(), TEXT("NullRHI")))
			{
				FScreenshotRequest::RequestScreenshot(
					FPaths::ProjectSavedDir() / TEXT("Extraction/BlueprintRefactor/HUD.png"), true, false);
				CapturedAt = FPlatformTime::Seconds();
				return false;
			}
			return true;
		}

	private:
		FAutomationTestBase* Test;
		int32 Clients;
		double Start = 0;
		double CapturedAt = 0;
	};

	class FRestoreExtractionPIE : public IAutomationLatentCommand
	{
	public:
		FRestoreExtractionPIE()
		{
			auto* S = GetDefault<ULevelEditorPlaySettings>();
			S->GetPlayNetMode(Mode);
			S->GetPlayNumberOfClients(Clients);
			S->GetRunUnderOneProcess(OneProcess);
			Separate = S->bLaunchSeparateServer;
		}

		bool Update() override
		{
			auto* S = GetMutableDefault<ULevelEditorPlaySettings>();
			S->SetPlayNetMode(Mode);
			S->SetPlayNumberOfClients(Clients);
			S->SetRunUnderOneProcess(OneProcess);
			S->bLaunchSeparateServer = Separate;
			return true;
		}

	private:
		EPlayNetMode Mode = PIE_Standalone;
		int32 Clients = 1;
		bool OneProcess = true, Separate = false;
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FExtractionBlueprintPIE, "BrokenEdge.Extraction.BlueprintPIE",
                                  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FExtractionBlueprintPIE::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Mode : {TEXT("Standalone"), TEXT("Listen"), TEXT("Clients")})
	{
		Names.Add(Mode);
		Commands.Add(Mode);
	}
}

bool FExtractionBlueprintPIE::RunTest(const FString& Mode)
{
	const auto Restore = MakeShared<FRestoreExtractionPIE>();
	auto* S = GetMutableDefault<ULevelEditorPlaySettings>();
	const int32 Clients = Mode == TEXT("Standalone") ? 1 : 2;
	S->SetPlayNetMode(Mode == TEXT("Standalone")
		                  ? PIE_Standalone
		                  : Mode == TEXT("Listen")
		                  ? PIE_ListenServer
		                  : PIE_Client);
	S->SetPlayNumberOfClients(Clients);
	S->SetRunUnderOneProcess(true);
	S->bLaunchSeparateServer = false;
	auto* World = FAutomationEditorCommonUtils::CreateNewMap();
	auto* ModeClass = LoadClass<AGameModeBase>(nullptr, TEXT("/Script/StingerTools.ExtractionBlueprintTestMode"));
	if (!TestNotNull(TEXT("Editor-only fixture mode"), ModeClass)) return false;
	World->GetWorldSettings()->DefaultGameMode = ModeClass;
	World->SpawnActor<APlayerStart>(FVector(0, 0, 200), FRotator::ZeroRotator);
	// Repeat PIE to catch stale widgets left behind when the world is torn down.
	for (int32 Round = 0; Round < 2; ++Round)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FCheckExtractionBlueprints>(this, Clients));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	}
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}
#endif
