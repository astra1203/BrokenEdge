// Development-only local process driver. No test RPCs or fixture commands exist in Shipping.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Engine/NetDriver.h"
#include "HighResScreenshot.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Button.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameSession.h"
#include "Kismet/GameplayStatics.h"
#include "MultiplayerSessionsSubsystem.h"
#include "Menu.h"
#include "BrokenEdge/Extraction/ExtractionGameMode.h"
#include "BrokenEdge/Extraction/ExtractionPlayerController.h"
#include "BrokenEdge/Extraction/ExtractionState.h"
#include "BrokenEdge/Extraction/ExtractionCombatComponent.h"
#include "BrokenEdge/GameState/LobbyGameState.h"
#include "BrokenEdge/UI/LobbyWidget.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Component/LagCompensationComponent.h"
#include "BrokenEdge/Weapon/Weapon.h"

class FExtractionDriver : public IAutomationLatentCommand
{
public:
	FExtractionDriver(FAutomationTestBase* InTest, FString InRole, FString InFolder)
		: Test(InTest), Role(MoveTemp(InRole)), Folder(MoveTemp(InFolder)), Started(FPlatformTime::Seconds())
	{
	}

	virtual bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - Started > 1500)
		{
			Test->AddError(TEXT("Extraction driver timeout"));
			return true;
		}
		UWorld* World = nullptr;
		for (const auto& Context : GEngine->GetWorldContexts())
			if (Context.WorldType == EWorldType::Game && Context.World() && Context.World()->HasBegunPlay())
				World =
					Context.World();
		if (!World) return false;
		auto* PC = World->GetFirstPlayerController();
		if (Role == TEXT("Server")) PC = nullptr;
		auto* Pawn = PC ? Cast<ABrokenEdgeCharacter>(PC->GetPawn()) : nullptr;
		if (PC && !AimTarget.IsEmpty())
		{
			if (auto* Target = FindPawn(World, AimTarget))
			{
				FVector Camera;
				FRotator Rotation;
				PC->GetPlayerViewPoint(Camera, Rotation);
				FVector Point = bHeadAim
					                ? Target->GetMesh()->GetBoneLocation(TEXT("head")) + FVector(0, 0, 8)
					                : Target->GetActorLocation() + FVector(0, 0, 10);
				PC->SetControlRotation((Point - Camera).Rotation());
			}
		}
		if (PC && !PulseAction.IsEmpty())
		{
			Inject(PC, PulseAction, Now < PulseEnd);
			if (Now >= PulseEnd) PulseAction.Reset();
		}
		FString Data;
		TSharedPtr<FJsonObject> Command;
		if (FFileHelper::LoadFileToString(Data, *(Folder / (Role + TEXT("-command.json"))))
			&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Data), Command) && Command.IsValid())
		{
			const int32 Id = Command->GetIntegerField(TEXT("id"));
			if (Id > LastCommand)
			{
				LastCommand = Id;
				Error.Reset();
				const FString Op = Command->GetStringField(TEXT("op"));
				if (Op == TEXT("finish"))
				{
					Test->AddInfo(TEXT("EXTRACTION_DRIVER_OK ") + Role);
					WriteState(World, PC);
					return true;
				}
				if (Op == TEXT("join"))
				{
					TArray<UUserWidget*> Widgets;
					UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, UMenu::StaticClass(), true);
					if (Widgets.Num() != 1) Error = TEXT("No room browser");
					else
					{
						auto* Menu = Cast<UMenu>(Widgets[0]);
						bool Found = false;
						for (int32 Index = 0; Index < Menu->GetRooms().Num(); ++Index)
						{
							FString Mode;
							Menu->GetRooms()[Index].Session.SessionSettings.Get(TEXT("MatchType"), Mode);
							if (Mode == TEXT("Extraction"))
							{
								Menu->JoinRoom(Index);
								Found = true;
								break;
							}
						}
						if (!Found)
						{
							if (!Menu->IsRequestPending()) Menu->RefreshRooms();
							Error = TEXT("Room not found yet");
						}
					}
				}
				else if (Op == TEXT("name") && PC) PC->SetName(TEXT("EX_") + Role);
				else if (Op == TEXT("direct_join") && PC)
				{
					bool bExpectRejection = false;
					Command->TryGetBoolField(TEXT("expect_rejection"), bExpectRejection);
					if (bExpectRejection)
						Test->AddExpectedError(TEXT("MATCH_IN_PROGRESS"), EAutomationExpectedErrorFlags::Contains, 0);
					PC->ClientTravel(TEXT("127.0.0.1:27777"), TRAVEL_Absolute);
				}
				else if (Op == TEXT("ready") && PC)
				{
					TArray<UUserWidget*> Widgets;
					UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, ULobbyWidget::StaticClass(), true);
					if (Widgets.Num() == 1)
					{
						if (auto* Button = Cast<UButton>(Widgets[0]->GetWidgetFromName(TEXT("ReadyButton"))))
							Button->
								OnClicked.Broadcast();
					}
					else Error = TEXT("No ready widget");
				}
				else if (Op == TEXT("pulse") && PC)
				{
					PulseAction = Command->GetStringField(TEXT("action"));
					double Duration = .035;
					Command->TryGetNumberField(TEXT("duration"), Duration);
					PulseEnd = Now + Duration;
					Inject(PC, PulseAction, true);
				}
				else if (Op == TEXT("aim"))
				{
					AimTarget = Command->GetStringField(TEXT("target"));
					bHeadAim = Command->GetBoolField(TEXT("head"));
					if (AimTarget.IsEmpty() && PC) PC->SetControlRotation(FRotator::ZeroRotator);
				}
				else if (Op == TEXT("fixture") && Role == TEXT("Server"))
				{
					for (const auto& Value : Command->GetArrayField(TEXT("players")))
					{
						const auto Item = Value->AsObject();
						auto* Character = FindPawn(World, Item->GetStringField(TEXT("name")));
						if (!Character)
						{
							Error = TEXT("Fixture player missing");
							continue;
						}
						const auto& Position = Item->GetArrayField(TEXT("position"));
						Character->GetCharacterMovement()->StopMovementImmediately();
						Character->SetActorLocation(
							FVector(Position[0]->AsNumber(), Position[1]->AsNumber(), Position[2]->AsNumber()), false,
							nullptr, ETeleportType::TeleportPhysics);
						Character->SetHealth(Item->GetNumberField(TEXT("health")));
						Character->SetShield(Item->GetNumberField(TEXT("shield")));
						Character->ForceNetUpdate();
					}
				}
				else if (Op == TEXT("ammo") && Role == TEXT("Server"))
				{
					auto* Character = FindPawn(World, Command->GetStringField(TEXT("target")));
					if (Character && Character->GetEquippedWeapon())
						Character->GetEquippedWeapon()->MulticastAmmo(
							Command->GetIntegerField(TEXT("value")));
				}
				else if (Op == TEXT("burst") && Pawn)
				{
					auto* Combat = Pawn->GetExtractionCombat();
					for (int32 I = 0; I < Command->GetIntegerField(TEXT("count")); ++I)
						Combat->RequestShot(
							Pawn->GetActorLocation() + FVector(0, 0, 8000));
				}
				else if (Op == TEXT("invalid") && Pawn)
				{
					auto* Combat = Pawn->GetExtractionCombat();
					const FString Kind = Command->GetStringField(TEXT("kind"));
					FVector End = Pawn->GetActorLocation() + FVector(0, 0, 8000);
					if (Kind == TEXT("duplicate")) Combat->ServerRequestShot(0, Pawn->GetEquippedWeapon(), End);
					else if (Kind == TEXT("foreign"))
					{
						auto* Other = FindPawn(World, Command->GetStringField(TEXT("target")));
						Combat->ServerRequestShot(++Combat->LocalShotId, Other ? Other->GetEquippedWeapon() : nullptr,
						                          End);
					}
					else if (Kind == TEXT("legacy"))
						Pawn->GetCombat()->ServerFire(
							End, Pawn->GetEquippedWeapon()->FireDelay);
					else if (Kind == TEXT("rewind"))
					{
						auto* Other = FindPawn(World, Command->GetStringField(TEXT("target")));
						if (Other)
						{
							const FVector Start = Pawn->GetActorLocation();
							const FVector Target = Other->GetMesh()->GetBoneLocation(TEXT("head"));
							const float HitTime = World->GetGameState()->GetServerWorldTimeSeconds();
							Pawn->GetLagCompensation()->HitScanServerScoreRequest(Other, Start, Target, HitTime);
							Pawn->GetLagCompensation()->ProjectileServerScoreRequest(
								Other, Start, (Target - Start).GetSafeNormal() * 15000, HitTime);
							Pawn->GetLagCompensation()->ShotgunServerScoreRequest({Other}, Start, {Target}, HitTime);
						}
					}
					else if (Kind == TEXT("range"))
						Combat->ServerRequestShot(
							++Combat->LocalShotId, Pawn->GetEquippedWeapon(),
							Pawn->GetActorLocation() + FVector(200000, 0, 0));
					else Combat->ServerRequestShot(++Combat->LocalShotId, Pawn->GetEquippedWeapon(), End);
				}
				else if (Op == TEXT("cycle") && PC)
				{
					if (auto* ExtractionPC = Cast<AExtractionPlayerController>(PC))
						ExtractionPC->ServerCycleSpectator(
							Command->GetIntegerField(TEXT("direction")));
				}
				else if (Op == TEXT("restart") && PC) PC->ServerRestartPlayer();
				else if (Op == TEXT("leave") && PC)
				{
					if (auto* ExtractionPC = Cast<AExtractionPlayerController>(PC))
						ExtractionPC->
							ServerLeaveExtraction();
				}
				else if (Op == TEXT("end") && Role == TEXT("Server"))
				{
					if (auto* Mode = World->GetAuthGameMode<AExtractionGameMode>())
						Mode->FinishRound(
							TEXT("Automation fixture end"));
				}
				else if (Op == TEXT("screenshot") && PC)
					FScreenshotRequest::RequestScreenshot(
						Folder / (Role + TEXT("-") + Command->GetStringField(TEXT("name")) + TEXT(".png")), true,
						false);
				else if (Op == TEXT("lag"))
				{
					GEngine->Exec(World, TEXT("NetEmulation.PktLag 50"));
					GEngine->Exec(World, TEXT("NetEmulation.PktLoss 2"));
				}
			}
		}
		if (Now - LastWrite > .1)
		{
			WriteState(World, PC);
			LastWrite = Now;
		}
		return false;
	}

private:
	FAutomationTestBase* Test;
	FString Role, Folder, Error, PulseAction, AimTarget;
	double Started, LastWrite = 0, PulseEnd = 0;
	int32 LastCommand = 0;
	bool bHeadAim = false;

	static ABrokenEdgeCharacter* FindPawn(UWorld* World, const FString& Name)
	{
		if (!World->GetGameState()) return nullptr;
		for (APlayerState* PS : World->GetGameState()->PlayerArray)
			if (PS->GetPlayerName() == Name) return Cast<ABrokenEdgeCharacter>(PS->GetPawn());
		return nullptr;
	}

	static void Inject(APlayerController* PC, const FString& Name, bool Pressed)
	{
		auto* Input = PC->GetLocalPlayer()
			              ? PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()
			              : nullptr;
		auto* Action = LoadObject<UInputAction>(
			nullptr, *(TEXT("/Game/Input/Actions/IA_") + Name + TEXT(".IA_") + Name));
		if (Input && Action) Input->InjectInputForAction(Action, FInputActionValue(Pressed), {}, {});
	}

	void WriteState(UWorld* World, APlayerController* PC)
	{
		auto State = MakeShared<FJsonObject>();
		State->SetStringField(TEXT("role"), Role);
		State->SetNumberField(TEXT("command"), LastCommand);
		State->SetStringField(TEXT("error"), Error);
		State->SetStringField(TEXT("map"), World->GetPackage()->GetName());
		State->SetNumberField(TEXT("net_mode"), World->GetNetMode());
		State->SetNumberField(TEXT("local_players"), World->GetGameInstance()->GetNumLocalPlayers());
		if (const UNetDriver* Driver = World->GetNetDriver())
		{
			State->SetNumberField(TEXT("packet_lag"), Driver->PacketSimulationSettings.PktLag);
			State->SetNumberField(TEXT("packet_loss"), Driver->PacketSimulationSettings.PktLoss);
		}
		if (auto* Mode = World->GetAuthGameMode())
		{
			State->SetStringField(TEXT("game_mode"), Mode->GetClass()->GetPathName());
			State->SetNumberField(TEXT("capacity"), Mode->GameSession->MaxPlayers);
		}
		if (auto* GS = World->GetGameState<AExtractionGameState>())
		{
			State->SetNumberField(TEXT("phase"), static_cast<int32>(GS->Phase));
			State->SetNumberField(TEXT("alive"), GS->AlivePlayers);
			State->SetNumberField(TEXT("remaining"), GS->Deadline - GS->GetServerWorldTimeSeconds());
		}
		if (auto* Sessions = World->GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>())
			State->SetBoolField(TEXT("session"), Sessions->HasSession());
		if (auto* ExtractPC = Cast<AExtractionPlayerController>(PC))
		{
			State->SetBoolField(TEXT("spectating"), ExtractPC->bExtractionSpectating);
			State->SetStringField(TEXT("spectated"), ExtractPC->SpectatedPlayer
				                                         ? ExtractPC->SpectatedPlayer->GetPlayerName()
				                                         : TEXT(""));
		}
		if (PC)
		{
			State->SetBoolField(TEXT("has_pawn"), PC->GetPawn() != nullptr);
			int32 Width, Height;
			PC->GetViewportSize(Width, Height);
			State->SetNumberField(TEXT("width"), Width);
			State->SetNumberField(TEXT("height"), Height);
		}
		TArray<TSharedPtr<FJsonValue>> Players;
		if (World->GetGameState())
			for (APlayerState* PS : World->GetGameState()->PlayerArray)
			{
				auto Player = MakeShared<FJsonObject>();
				Player->SetStringField(TEXT("name"), PS->GetPlayerName());
				Player->SetNumberField(TEXT("id"), PS->GetPlayerId());
				if (const auto* LobbyPS = Cast<ABrokenEdgePlayerState>(PS))
					Player->SetBoolField(
						TEXT("ready"), LobbyPS->IsLobbyReady());
				if (auto* ExtractionPS = Cast<AExtractionPlayerState>(PS))
					Player->SetNumberField(
						TEXT("life"), static_cast<int32>(ExtractionPS->Life));
				if (auto* Character = Cast<ABrokenEdgeCharacter>(PS->GetPawn()))
				{
					Player->SetNumberField(TEXT("health"), Character->GetHealth());
					Player->SetNumberField(TEXT("shield"), Character->GetShield());
					Player->SetBoolField(TEXT("disabled"), Character->bDisableGameplay);
					Player->SetBoolField(TEXT("defeated"), Character->IsDefeated());
					Player->SetBoolField(TEXT("first_person"), Character->IsFirstPersonView());
					Player->SetStringField(TEXT("pawn_class"), Character->GetClass()->GetPathName());
					Player->SetStringField(TEXT("body_anim"), GetPathNameSafe(Character->GetMesh()->GetAnimInstance()));
					if (auto* ViewMesh = Character->GetFirstPersonMesh())
					{
						Player->SetNumberField(TEXT("fp_lods"), ViewMesh->GetNumLODs());
						const int32 Gear = ViewMesh->GetMaterialIndex(TEXT("M_Roy_016_BP_LOD"));
						Player->SetNumberField(TEXT("fp_gear_slot"), Gear);
						Player->SetBoolField(TEXT("fp_gear_visible"), ViewMesh->IsMaterialSectionShown(Gear, 0));
					}
					Player->SetStringField(TEXT("overlap"), Character->GetOverlappingWeapon()
						                                        ? Character->GetOverlappingWeapon()->GetName()
						                                        : TEXT(""));
					Player->SetNumberField(TEXT("accepted"), Character->GetExtractionCombat()->GetAcceptedShots());
					Player->SetNumberField(TEXT("rejected"), Character->GetExtractionCombat()->GetRejectedShots());
					Player->SetNumberField(TEXT("combat_state"), static_cast<int32>(Character->GetCombatState()));
					Player->SetNumberField(TEXT("carried_ammo"), Character->GetCombat()->GetCarriedAmmo());
					const FVector P = Character->GetActorLocation();
					Player->SetArrayField(TEXT("position"), {
						                      MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y),
						                      MakeShared<FJsonValueNumber>(P.Z)
					                      });
					if (auto* Weapon = Character->GetEquippedWeapon())
					{
						Player->SetNumberField(
							TEXT("left_grip_distance"),
							FVector::Dist(Character->GetMesh()->GetSocketLocation(TEXT("LeftHand")),
							              Weapon->GetWeaponMesh()->GetSocketLocation(TEXT("LeftHandSocket"))));
						Player->SetNumberField(
							TEXT("right_grip_distance"),
							FVector::Dist(Character->GetMesh()->GetSocketLocation(TEXT("RightHand")),
							              Weapon->GetWeaponMesh()->GetSocketLocation(TEXT("Hand_R"))));
						Player->SetStringField(TEXT("weapon"), Weapon->GetName());
						Player->SetNumberField(TEXT("ammo"), Weapon->GetAmmo());
						Player->SetNumberField(TEXT("damage"), Weapon->GetDamage());
						Player->SetNumberField(TEXT("head_damage"), Weapon->GetHeadShotDamage());
					}
				}
				Players.Add(MakeShared<FJsonValueObject>(Player));
			}
		State->SetArrayField(TEXT("players"), Players);
		TArray<TSharedPtr<FJsonValue>> Weapons;
		for (TActorIterator<AWeapon> It(World); It; ++It)
		{
			auto Weapon = MakeShared<FJsonObject>();
			Weapon->SetStringField(TEXT("name"), It->GetName());
			Weapon->SetNumberField(TEXT("ammo"), It->GetAmmo());
			Weapon->SetNumberField(TEXT("state"), static_cast<int32>(It->GetWeaponState()));
			Weapon->SetStringField(TEXT("owner"), GetNameSafe(It->GetOwner()));
			const FVector Position = It->GetActorLocation();
			Weapon->SetArrayField(TEXT("position"), {
				                      MakeShared<FJsonValueNumber>(Position.X),
				                      MakeShared<FJsonValueNumber>(Position.Y), MakeShared<FJsonValueNumber>(Position.Z)
			                      });
			Weapons.Add(MakeShared<FJsonValueObject>(Weapon));
		}
		State->SetArrayField(TEXT("weapons"), Weapons);
		FString Json;
		FJsonSerializer::Serialize(State, TJsonWriterFactory<>::Create(&Json));
		const FString File = Folder / (Role + TEXT("-state.json"));
		FFileHelper::SaveStringToFile(Json, *(File + TEXT(".tmp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		IFileManager::Get().Move(*File, *(File + TEXT(".tmp")), true);
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionProcessDriver, "BrokenEdge.Extraction.ProcessDriver",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext |
                                 EAutomationTestFlags::ProductFilter)

bool FExtractionProcessDriver::RunTest(const FString& Parameters)
{
	FString Role, Folder;
	FParse::Value(FCommandLine::Get(),TEXT("ExtractionTestRole="), Role);
	FParse::Value(FCommandLine::Get(),TEXT("ExtractionTestDir="), Folder);
	if ((Role != TEXT("Server") && Role != TEXT("A") && Role != TEXT("B") && Role != TEXT("C")) || Folder.IsEmpty())
	{
		AddError(TEXT("Use Scripts/Extraction/run_network.py"));
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FExtractionDriver(this,Role,Folder));
	return true;
}
#endif
