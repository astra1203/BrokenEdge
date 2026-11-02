// Development-only local process driver. No test RPCs or fixture commands exist in Shipping.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Extraction/ExtractionMeleeComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "HAL/PlatformMemory.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Engine/NetDriver.h"
#include "HighResScreenshot.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputKeyEventArgs.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "BrokenEdge/Extraction/ExtractionWidget.h"
#include "BrokenEdge/UI/LobbyShowcase.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameSession.h"
#include "Kismet/GameplayStatics.h"
#include "MultiplayerSessionsSubsystem.h"
#include "Menu.h"
#include "BrokenEdge/Extraction/ExtractionGameMode.h"
#include "BrokenEdge/Extraction/ExtractionRegionStream.h"
#include "BrokenEdge/Extraction/ExtractionAbilities.h"
#include "BrokenEdge/Extraction/ExtractionHealth.h"
#include "BrokenEdge/Extraction/ExtractionGuard.h"
#include "BrokenEdge/Character/GuardBlackboardKeys.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BrokenEdge/Extraction/ExtractionSmoke.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "BrokenEdge/Extraction/ExtractionPlayerController.h"
#include "BrokenEdge/Extraction/ExtractionState.h"
#include "BrokenEdge/Extraction/ExtractionCombatComponent.h"
#include "BrokenEdge/Extraction/ExtractionInteractionComponent.h"
#include "BrokenEdge/Extraction/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/ExtractionLootActor.h"
#include "BrokenEdge/Extraction/ExtractionAccountComponent.h"
#include "BrokenEdge/Extraction/ExtractionKeyRoom.h"
#include "BrokenEdge/Extraction/ExtractionExit.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "BrokenEdge/GameState/LobbyGameState.h"
#include "BrokenEdge/UI/LobbyWidget.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Component/LagCompensationComponent.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "TraversalAcceptanceFixture.h"
#include "BrokenEdge/Traversal/TraversalMovementComponent.h"
#include "BrokenEdge/Traversal/ExtractionTraversalComponent.h"
#include "BrokenEdge/Traversal/ExtractionLadder.h"

class FExtractionDriver : public IAutomationLatentCommand
{
public:
	FExtractionDriver(FAutomationTestBase* InTest, FString InRole, FString InFolder)
		: Test(InTest), Role(MoveTemp(InRole)), Folder(MoveTemp(InFolder)), Started(FPlatformTime::Seconds())
	{
		FParse::Value(FCommandLine::Get(), TEXT("ExtractionTestTimeout="), TimeoutSeconds);
		TimeoutSeconds = FMath::Clamp(TimeoutSeconds, 60.0, 7200.0);
	}

	virtual bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - Started > TimeoutSeconds)
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
		if (Pawn && WalkIndex < WalkPoints.Num())
		{
			const FVector Offset = WalkPoints[WalkIndex] - Pawn->GetActorLocation();
			if (Offset.Size2D() < 45)
			{
				++WalkIndex;
				WalkSince = Now;
				Inject(PC, TEXT("Move"), false);
			}
			else if (Now - WalkSince > 45)
			{
				Error = FString::Printf(TEXT("Walk stalled at waypoint %d"), WalkIndex);
				WalkPoints.Empty();
				Inject(PC, TEXT("Move"), false);
			}
			else
			{
				PC->SetControlRotation(FRotator(0, Offset.Rotation().Yaw, 0));
				Inject(PC, TEXT("Move"), true);
			}
		}
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
				if (!AimBone.IsNone()) Point = Target->GetMesh()->GetBoneLocation(AimBone);
				if (bBodyCenterAim && Target->GetMesh()->GetBodyInstance(AimBone))
					Point = Target->GetMesh()->GetCenterOfMass(AimBone);
				PC->SetControlRotation((Point - Camera).Rotation());
			}
		}
		if (PC && !PulseAction.IsEmpty())
		{
			Inject(PC, PulseAction, Now < PulseEnd);
			if (Now >= PulseEnd) PulseAction.Reset();
		}
		if (PC && !Recording.IsEmpty() && Now - LastFrame >= 1.0 / 30.0 && !FScreenshotRequest::IsScreenshotRequested())
		{
			const FString Frame = FString::Printf(TEXT("%06d.png"), RecordingFrame++);
			const FString Directory = Folder / (Role + TEXT("-") + Recording);
			IFileManager::Get().MakeDirectory(*Directory, true);
			FScreenshotRequest::RequestScreenshot(Directory / Frame, true, false);
			FFileHelper::SaveStringToFile(FString::Printf(TEXT("%s,%.6f\n"), *Frame, Now),
			                              *(Directory / TEXT("Frames.csv")),
			                              FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(),
			                              FILEWRITE_Append);
			LastFrame = Now;
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
						FString RequiredRoom;
						FParse::Value(FCommandLine::Get(), TEXT("ExtractionTestRoom="), RequiredRoom);
						for (int32 Index = 0; Index < Menu->GetRooms().Num(); ++Index)
						{
							FString Mode;
							Menu->GetRooms()[Index].Session.SessionSettings.Get(TEXT("MatchType"), Mode);
							FString Room;
							Menu->GetRooms()[Index].Session.SessionSettings.Get(TEXT("RoomName"), Room);
							if (Mode == TEXT("Extraction") && (RequiredRoom.IsEmpty() || Room == RequiredRoom))
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
				else if (Op == TEXT("squad") && PC)
				{
					if (auto* LobbyPC = Cast<ABrokenEdgePlayerController>(PC))
						LobbyPC->ServerSetExtractionSquad(
							Command->GetIntegerField(TEXT("value")));
				}
				else if (Op == TEXT("ability") && Pawn)
				{
					const FString Kind = Command->GetStringField(TEXT("kind"));
					FString Name;
					Command->TryGetStringField(TEXT("target"), Name);
					auto* Target = Name.IsEmpty() ? Pawn : FindPawn(World, Name);
					auto* A = Pawn->GetExtractionAbilities();
					// 激素枪射向队友是真实飞镖，像玩家一样先把准星对准目标胸口，命中仍由服务器弹道判定。
					if (Kind == TEXT("heal") && Target && Target != Pawn && PC)
						PC->SetControlRotation((Target->GetActorLocation() - Pawn->GetPawnViewLocation()).Rotation());
					if (Kind == TEXT("cancel")) A->ReleaseRescue();
					else if (Kind == TEXT("duplicate")) A->ServerBegin(0, EExtractionAction::Heal, Target, false);
					else
						A->Request(Kind == TEXT("revive")
							           ? EExtractionAction::Revive
							           : Kind == TEXT("heal")
							           ? EExtractionAction::Heal
							           : EExtractionAction::Smoke, Target);
				}
				else if (Op == TEXT("exit_config") && Role == TEXT("Server"))
				{
					const FName ExitId(*Command->GetStringField(TEXT("exit_id")));
					AExtractionExit* Exit = nullptr;
					for (TActorIterator<AExtractionExit> It(World); It; ++It)
						if (It->Definition.Id == ExitId)
						{
							Exit = *It;
							break;
						}
					if (!Exit) Error = TEXT("Unknown test exit");
					else
					{
						auto Definition = Exit->Definition;
						double Value;
						if (Command->TryGetNumberField(TEXT("opens_after"), Value))
							Definition.OpensAfterSeconds =
								Value;
						if (Command->TryGetNumberField(TEXT("closes_after"), Value))
							Definition.ClosesAfterSeconds =
								Value;
						if (Command->TryGetNumberField(TEXT("hold_seconds"), Value)) Definition.HoldSeconds = Value;
						if (Command->TryGetNumberField(TEXT("capacity"), Value)) Definition.Capacity = Value;
						if (!Definition.IsValid()) Error = TEXT("Invalid test exit definition");
						else
						{
							Exit->Configure(Definition);
							bool ResetStart = false;
							if (Command->TryGetBoolField(TEXT("reset_start"), ResetStart) && ResetStart)
								Exit->StartRound(World->GetTimeSeconds());
							if (Command->TryGetNumberField(TEXT("used"), Value))
								Exit->UsedCapacity = FMath::Max(
									0, static_cast<int32>(Value));
							Exit->ForceNetUpdate();
						}
					}
				}
				else if (Op == TEXT("guard_spawn") && Role == TEXT("Server"))
				{
					if (auto* Mode = World->GetAuthGameMode<AExtractionGameMode>()) Mode->SpawnGuards();
				}
				else if (Op == TEXT("name") && PC) PC->SetName(TEXT("EX_") + Role);
				else if (HandleInventoryCommand(Command, PC, World))
				{
				}
				else if (Op == TEXT("direct_join") && PC)
				{
					bool bExpectRejection = false;
					Command->TryGetBoolField(TEXT("expect_rejection"), bExpectRejection);
					if (bExpectRejection)
					{
						FString Reason = TEXT("MATCH_IN_PROGRESS");
						Command->TryGetStringField(TEXT("reason"), Reason);
						Test->AddExpectedError(Reason, EAutomationExpectedErrorFlags::Contains, 0);
					}
					int32 Port = 27777;
					FParse::Value(FCommandLine::Get(), TEXT("ExtractionTestPort="), Port);
					if (Port < 1024 || Port > 65535) Error = TEXT("Invalid test server port");
					else PC->ClientTravel(FString::Printf(TEXT("127.0.0.1:%d"), Port), TRAVEL_Absolute);
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
					FString Bone;
					Command->TryGetStringField(TEXT("bone"), Bone);
					AimBone = FName(*Bone);
					bBodyCenterAim = false;
					Command->TryGetBoolField(TEXT("body_center"), bBodyCenterAim);
					if (AimTarget.IsEmpty() && PC) PC->SetControlRotation(FRotator::ZeroRotator);
				}
				else if (Op == TEXT("look") && PC)
				{
					AimTarget.Reset();
					double Pitch = 0;
					Command->TryGetNumberField(TEXT("pitch"), Pitch);
					PC->SetControlRotation(FRotator(Pitch, Command->GetNumberField(TEXT("yaw")), 0));
				}
				else if (Op == TEXT("fixture") && (Role == TEXT("Server") || World->GetNetMode() == NM_Standalone))
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
						double Yaw = 0;
						if (Item->TryGetNumberField(TEXT("yaw"), Yaw))
						{
							Character->SetActorRotation(FRotator(0, Yaw, 0));
							if (Character->GetController())
								Character->GetController()->SetControlRotation(
									FRotator(0, Yaw, 0));
						}
						Character->SetHealth(Item->GetNumberField(TEXT("health")));
						Character->SetShield(Item->GetNumberField(TEXT("shield")));
						bool ClearTrauma = false;
						if (Item->TryGetBoolField(TEXT("clear_trauma"), ClearTrauma) && ClearTrauma)
						{
							// Baseline precondition only; equipment acceptance uses medicine/rescue.
							Character->GetExtractionHealth()->Bleeding = false;
							Character->GetExtractionHealth()->LegFracture = false;
							Character->GetExtractionHealth()->ClearStim();
						}
						Character->ForceNetUpdate();
					}
				}
				else if (Op == TEXT("ammo") && Role == TEXT("Server"))
				{
					auto* Character = FindPawn(World, Command->GetStringField(TEXT("target")));
					if (Character && Character->GetEquippedWeapon())
					{
						Character->GetEquippedWeapon()->MulticastAmmo(
							Command->GetIntegerField(TEXT("value")));
						if (auto* Inv = Character->GetExtractionInteraction()->Inventory())
						{
							Inv->CaptureWeapons();
							for (auto& Item : Inv->Contents.Items)
								if (Item.Id == Character->GetEquippedWeapon()->ExtractionItemId)
								{
									Item.MagazineOrigins.Empty();
									if (Item.Magazine > 0) Item.MagazineOrigins.Add({FGuid::NewGuid(), Item.Magazine});
								}
							Inv->Changed();
						}
					}
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
					if (Kind == TEXT("version"))
						Combat->ServerRequestShotVersioned(
							++Combat->LocalShotId, Pawn->GetEquippedWeapon(), End, 0);
					else if (Kind == TEXT("duplicate")) Combat->ServerRequestShot(0, Pawn->GetEquippedWeapon(), End);
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
				else if (Op == TEXT("visual_tune") && FParse::Param(FCommandLine::Get(),
				                                                    TEXT("ExtractionVisualCalibration")))
				{
					// Isolated visual calibration only; never persists assets or modifies item outcomes.
					auto* Definition = const_cast<UExtractionItemDefinition*>(UExtractionItemDefinition::Get(
						TEXT("VerticalGrip")));
					if (Definition)
					{
						const FTransform OldAdapter = Definition->AdapterMount;
						auto ReadVector = [&](const TCHAR* Field)
						{
							const auto& Values = Command->GetArrayField(Field);
							return FVector(Values[0]->AsNumber(), Values[1]->AsNumber(), Values[2]->AsNumber());
						};
						if (Command->HasField(TEXT("grip")))
							Definition->SupportGrip.SetLocation(
								ReadVector(TEXT("grip")));
						if (Command->HasField(TEXT("rotation")))
						{
							const FVector Angles = ReadVector(TEXT("rotation"));
							Definition->SupportGrip.SetRotation(FRotator(Angles.X, Angles.Y, Angles.Z).Quaternion());
						}
						if (Command->HasField(TEXT("mount")))
						{
							Definition->Mount.SetLocation(ReadVector(TEXT("mount")));
							Definition->FirstPersonMount = Definition->Mount;
						}
						if (Command->HasField(TEXT("adapter")))
							Definition->AdapterMount.SetLocation(
								ReadVector(TEXT("adapter")));
						for (TActorIterator<AWeapon> It(World); It; ++It)
						{
							TInlineComponentArray<UStaticMeshComponent*> Components(*It);
							for (auto* Part : Components)
							{
								if (Part->GetStaticMesh() == Definition->WorldMesh)
									Part->SetRelativeTransform(
										Definition->Mount);
								else if (Part->GetStaticMesh() == Definition->FirstPersonMesh)
									Part->
										SetRelativeTransform(Definition->FirstPersonMount);
								else if (Part->GetStaticMesh() == Definition->AdapterMesh && Part->
									GetRelativeTransform().Equals(OldAdapter))
									Part->SetRelativeTransform(Definition->AdapterMount);
							}
						}
					}
				}
				else if (Op == TEXT("pose"))
				{
					if (auto* Target = FindPawn(World, Command->GetStringField(TEXT("target"))); Target && Target->
						GetEquippedWeapon())
					{
						auto Probe = MakeShared<FJsonObject>();
						Probe->SetBoolField(TEXT("crouched"), Target->bIsCrouched);
						auto WriteMesh = [&](const TCHAR* Name, USkeletalMeshComponent* Body,
						                     USkeletalMeshComponent* WeaponMesh)
						{
							if (!Body || !WeaponMesh) return;
							auto Bones = MakeShared<FJsonObject>();
							for (int32 Index = 0; Index < Body->GetNumBones(); ++Index)
							{
								const FName Bone = Body->GetBoneName(Index);
								if (!Bone.ToString().Contains(TEXT("LeftHand")) && Bone != TEXT("RightHand") && Bone !=
									TEXT("LeftFoot") && Bone != TEXT("RightFoot") && Bone != TEXT("Head") && Bone !=
									TEXT(
										"Neck")
									&& Bone != TEXT("RightShoulder") && Bone != TEXT("LeftShoulder") && Bone != TEXT(
										"Spine2"))
									continue;
								const FTransform Local = Body->GetSocketTransform(Bone).GetRelativeTransform(
									WeaponMesh->GetComponentTransform());
								auto Value = MakeShared<FJsonObject>();
								const FVector Position = Local.GetLocation();
								Value->SetArrayField(TEXT("position"), {
									                     MakeShared<FJsonValueNumber>(Position.X),
									                     MakeShared<FJsonValueNumber>(Position.Y),
									                     MakeShared<FJsonValueNumber>(Position.Z)
								                     });
								Value->SetStringField(TEXT("rotation"), Local.Rotator().ToString());
								Value->SetStringField(TEXT("world_position"), Body->GetBoneLocation(Bone).ToString());
								if (Body->GetBodyInstance(Bone))
									Value->SetStringField(
										TEXT("collision_center"), Body->GetCenterOfMass(Bone).ToString());
								Bones->SetObjectField(Bone.ToString(), Value);
							}
							Probe->SetObjectField(Name, Bones);
						};
						WriteMesh(TEXT("world"), Target->GetMesh(), Target->GetEquippedWeapon()->GetWeaponMesh());
						if (Target->IsLocallyControlled())
							WriteMesh(
								TEXT("first_person"), Target->GetFirstPersonMesh(), Target->GetFirstPersonWeaponMesh());
						FString Output;
						FJsonSerializer::Serialize(Probe, TJsonWriterFactory<>::Create(&Output));
						FFileHelper::SaveStringToFile(
							Output,
							*(Folder / (Role + TEXT("-") + Command->GetStringField(TEXT("name")) + TEXT(
								"-pose.json"))));
					}
				}
				else if (Op == TEXT("world_camera") && PC)
				{
					const auto& Position = Command->GetArrayField(TEXT("position"));
					const auto& Target = Command->GetArrayField(TEXT("target"));
					if (Position.Num() != 3 || Target.Num() != 3) Error = TEXT("Camera requires two world vectors");
					else
					{
						const FVector Location(Position[0]->AsNumber(), Position[1]->AsNumber(),
						                       Position[2]->AsNumber());
						const FVector Center(Target[0]->AsNumber(), Target[1]->AsNumber(), Target[2]->AsNumber());
						if (!TestCamera.IsValid()) TestCamera = World->SpawnActor<ACameraActor>();
						TestCamera->SetActorLocationAndRotation(Location, (Center - Location).Rotation());
						TestCamera->GetCameraComponent()->
						            SetFieldOfView(Command->GetNumberField(TEXT("field_of_view")));
						TestCamera->GetCameraComponent()->SetAspectRatio(1920.f / 1080.f);
						PC->SetViewTarget(TestCamera.Get());
					}
				}
				else if (Op == TEXT("camera") && PC)
				{
					const FString TargetName = Command->GetStringField(TEXT("target"));
					if (TargetName.IsEmpty())
					{
						PC->SetViewTarget(PC->GetPawn());
						if (TestCamera.IsValid()) TestCamera->Destroy();
						TestCamera.Reset();
					}
					else if (auto* Target = FindPawn(World, TargetName); Target && Target->GetEquippedWeapon())
					{
						if (!TestCamera.IsValid()) TestCamera = World->SpawnActor<ACameraActor>();
						const FTransform Transform = Target->GetEquippedWeapon()->GetWeaponMesh()->
						                                     GetComponentTransform();
						const auto& Offset = Command->GetArrayField(TEXT("offset"));
						const FVector Center = Transform.TransformPosition(FVector(0, 25, 2));
						const FVector Location = Center + Transform.TransformVectorNoScale(FVector(
							Offset[0]->AsNumber(), Offset[1]->AsNumber(), Offset[2]->AsNumber()));
						TestCamera->SetActorLocationAndRotation(Location, (Center - Location).Rotation());
						TestCamera->GetCameraComponent()->SetFieldOfView(45.f);
						PC->SetViewTarget(TestCamera.Get());
					}
					else Error = TEXT("Camera target has no equipped weapon");
				}
				else if (Op == TEXT("retry_loadout") && World->GetNetMode() != NM_Client)
				{
					if (auto* Target = FindPawn(World, Command->GetStringField(TEXT("target"))))
						if (auto* State = Target->GetPlayerState<AExtractionPlayerState>())
							State->Inventory->
							       InitializeLoadout();
				}
				else if (Op == TEXT("screenshot") && PC)
				{
					bool bShowUI = true;
					Command->TryGetBoolField(TEXT("show_ui"), bShowUI);
					FScreenshotRequest::RequestScreenshot(
						Folder / (Role + TEXT("-") + Command->GetStringField(TEXT("name")) + TEXT(".png")), bShowUI,
						false);
				}
				else if (Op == TEXT("walk") && Pawn)
				{
					WalkPoints.Empty();
					WalkIndex = 0;
					WalkSince = Now;
					AimTarget.Reset();
					for (const auto& Value : Command->GetArrayField(TEXT("points")))
					{
						const auto& P = Value->AsArray();
						if (P.Num() == 3) WalkPoints.Add(FVector(P[0]->AsNumber(), P[1]->AsNumber(), P[2]->AsNumber()));
					}
					if (WalkPoints.IsEmpty()) Inject(PC, TEXT("Move"), false);
				}
				else if (Op == TEXT("profile"))
				{
#if CSV_PROFILER
					if (Command->GetBoolField(TEXT("start")))
					{
						const FString Name = FPaths::GetCleanFilename(Command->GetStringField(TEXT("name")));
						FCsvProfiler::Get()->BeginCapture(-1, Folder, Role + TEXT("-") + Name + TEXT(".csv"));
					}
					else FCsvProfiler::Get()->EndCapture();
#else
					Error = TEXT("CSV profiler unavailable");
#endif
				}
				else if (Op == TEXT("region_delay"))
				{
					if (auto* Region = AExtractionRegionStream::Find(World))
						Region->SetTestLoadDelay(
							Command->GetNumberField(TEXT("seconds")));
				}
				else if (Op == TEXT("traversal_fixture") && Role == TEXT("Server"))
				{
					World->SpawnActor<ATraversalAcceptanceFixture>(FVector(30000, 30000, 6000), FRotator::ZeroRotator);
				}
				else if (Op == TEXT("record") && PC)
				{
					Recording = Command->GetBoolField(TEXT("enabled"))
						            ? FPaths::GetCleanFilename(Command->GetStringField(TEXT("name")))
						            : FString();
					RecordingFrame = 0;
				}
				else if (Op == TEXT("fps"))
				{
					const int32 Cap = FMath::Clamp(static_cast<int32>(Command->GetNumberField(TEXT("value"))), 15, 120);
					GEngine->Exec(World, *FString::Printf(TEXT("t.MaxFPS %d"), Cap));
				}
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
	FString Recording;
	int32 RecordingFrame = 0;
	double LastFrame = 0;
	TArray<FVector> WalkPoints;
	int32 WalkIndex = 0;
	double WalkSince = 0;
	double Started, LastWrite = 0, PulseEnd = 0;
	double TimeoutSeconds = 1500;
	int32 LastCommand = 0;
	bool bHeadAim = false;
	bool bBodyCenterAim = false;
	FName AimBone;
	FExtractionItemRequest LastItemRequest;
	FExtractionStorageRequest LastStorageRequest;
	TMap<FString, FExtractionEconomyRequest> EconomyRequests;
	FVector2D LastMouse = FVector2D::ZeroVector;
	TSet<FKey> PressedMouse;
	TWeakObjectPtr<AActor> TestOccluder;
	TWeakObjectPtr<ACameraActor> TestCamera;

	static void EquipmentRequest(const TSharedPtr<FJsonObject>& Command, FExtractionItemRequest& R)
	{
		FString Kind, Weapon, Ammo, Slot;
		Command->TryGetStringField(TEXT("kind"), Kind);
		if (Kind == TEXT("drop")) R.Op = EExtractionItemOp::Drop;
		else if (Kind == TEXT("use")) R.Op = EExtractionItemOp::Use;
		else if (Kind == TEXT("attach")) R.Op = EExtractionItemOp::Attach;
		else if (Kind == TEXT("detach")) R.Op = EExtractionItemOp::Detach;
		else if (Kind == TEXT("unload")) R.Op = EExtractionItemOp::Unload;
		else if (Kind == TEXT("select_ammo")) R.Op = EExtractionItemOp::SelectAmmo;
		if (Command->TryGetStringField(TEXT("weapon"), Weapon)) FGuid::Parse(Weapon, R.Weapon);
		if (Command->TryGetStringField(TEXT("ammo_type"), Ammo)) R.AmmoDefinition = FName(*Ammo);
		Command->TryGetStringField(TEXT("attachment_slot"), Slot);
		R.AttachmentSlot = Slot == TEXT("optic")
			                   ? EExtractionAttachmentSlot::Optic
			                   : Slot == TEXT("grip")
			                   ? EExtractionAttachmentSlot::Grip
			                   : Slot == TEXT("muzzle")
			                   ? EExtractionAttachmentSlot::Muzzle
			                   : EExtractionAttachmentSlot::None;
	}

	static TSharedPtr<FJsonObject> InventoryJson(const FExtractionContainer& C)
	{
		auto Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("id"), C.Id.ToString());
		Json->SetNumberField(TEXT("revision"), C.Revision);
		TArray<TSharedPtr<FJsonValue>> Entries;
		for (const auto& I : C.Items)
		{
			auto Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("id"), I.Id.ToString());
			Entry->SetStringField(TEXT("definition"), I.Definition.ToString());
			Entry->SetNumberField(TEXT("quantity"), I.Quantity);
			Entry->SetNumberField(TEXT("sell_price"), GetDefault<UExtractionProgressionRules>()->SellPrice(I, I.Quantity));
			Entry->SetNumberField(TEXT("x"), I.Cell.X);
			Entry->SetNumberField(TEXT("y"), I.Cell.Y);
			Entry->SetNumberField(TEXT("slot"), I.Slot);
			Entry->SetNumberField(TEXT("magazine"), I.Magazine);
			Entry->SetStringField(TEXT("ammo_type"), I.MagazineAmmo.ToString());
			Entry->SetNumberField(TEXT("durability"), I.Durability);
			Entry->SetNumberField(TEXT("scatter_scale"), ExtractionItems::ScatterScale(I));
			TArray<TSharedPtr<FJsonValue>> Parts;
			for (const auto& Part : I.Attachments)
			{
				auto J = MakeShared<FJsonObject>();
				J->SetStringField(TEXT("id"), Part.Id.ToString());
				J->SetStringField(TEXT("definition"), Part.Definition.ToString());
				Parts.Add(MakeShared<FJsonValueObject>(J));
			}
			Entry->SetArrayField(TEXT("attachments"), Parts);
			Entry->SetBoolField(TEXT("rotated"), I.Rotated);
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Json->SetArrayField(TEXT("items"), Entries);
		return Json;
	}

	static TSharedPtr<FJsonObject> AccountJson(const UExtractionAccountComponent* A)
	{
		auto J = MakeShared<FJsonObject>();
		J->SetBoolField(TEXT("bound"), A->Bound);
		J->SetBoolField(TEXT("locked"), A->Locked);
		J->SetStringField(TEXT("profile"), A->Profile.Profile.ToString());
		J->SetStringField(TEXT("message"), A->Message);
		J->SetNumberField(TEXT("revision"), A->Profile.Revision);
		J->SetObjectField(TEXT("progression"), ProgressionJson(A->Profile.Progression));
		J->SetNumberField(TEXT("outcome"), static_cast<int32>(A->Profile.Result.Outcome));
		J->SetObjectField(TEXT("stash"), InventoryJson(A->Profile.Stash));
		J->SetObjectField(TEXT("loadout"), InventoryJson(A->Profile.Loadout));
		FExtractionContainer Overflow;
		Overflow.Items = A->Profile.Overflow;
		J->SetObjectField(TEXT("overflow"), InventoryJson(Overflow));
		return J;
	}

	static TSharedPtr<FJsonObject> ProgressionJson(const FExtractionProgression& P)
	{
		auto J = MakeShared<FJsonObject>();
		J->SetNumberField(TEXT("credits"), P.Credits);
		J->SetNumberField(TEXT("experience"), P.Experience);
		J->SetNumberField(TEXT("level"), GetDefault<UExtractionProgressionRules>()->Level(P.Experience));
		TArray<TSharedPtr<FJsonValue>> Quests;
		for (const auto& Q : P.Quests)
		{
			auto V = MakeShared<FJsonObject>();
			V->SetBoolField(TEXT("accepted"), Q.Accepted);
			V->SetBoolField(TEXT("claimed"), Q.Claimed);
			V->SetNumberField(TEXT("progress"), Q.Progress);
			V->SetBoolField(TEXT("delivered"), Q.Delivered);
			V->SetNumberField(TEXT("eligible_files"), Q.EligibleFiles.Num());
			Quests.Add(MakeShared<FJsonValueObject>(V));
		}
		J->SetArrayField(TEXT("quests"), Quests);
		return J;
	}

	bool HandleInventoryCommand(const TSharedPtr<FJsonObject>& Command, APlayerController* Controller, UWorld* World)
	{
		const FString Op = Command->GetStringField(TEXT("op"));
		auto* PC = Cast<AExtractionPlayerController>(Controller);
		auto* Interaction = PC ? PC->Interaction() : nullptr;
		auto* Account = Controller ? Controller->FindComponentByClass<UExtractionAccountComponent>() : nullptr;
		if (Op == TEXT("economy") && Account)
		{
			FString Saved = TEXT("last"), Replay, Kind, Definition, Item;
			Command->TryGetStringField(TEXT("save"), Saved);
			FExtractionEconomyRequest R;
			if (Command->TryGetStringField(TEXT("replay"), Replay) && EconomyRequests.Contains(Replay))
				R =
					EconomyRequests[Replay];
			else
			{
				R.Operation = FGuid::NewGuid();
				R.Revision = Account->Profile.Revision;
				Command->TryGetStringField(TEXT("kind"), Kind);
				R.Action = Kind == TEXT("sell")
					           ? EExtractionEconomyAction::Sell
					           : Kind == TEXT("accept")
					           ? EExtractionEconomyAction::AcceptQuest
					           : Kind == TEXT("deliver")
					           ? EExtractionEconomyAction::DeliverQuest
					           : Kind == TEXT("claim")
					           ? EExtractionEconomyAction::ClaimQuest
					           : EExtractionEconomyAction::Buy;
				Command->TryGetStringField(TEXT("definition"), Definition);
				R.Definition = FName(*Definition);
				Command->TryGetStringField(TEXT("item"), Item);
				FGuid::Parse(Item, R.Item);
				Command->TryGetNumberField(TEXT("quantity"), R.Quantity);
				Command->TryGetNumberField(TEXT("quest"), R.Quest);
			}
			Command->TryGetNumberField(TEXT("revision"), R.Revision);
			EconomyRequests.Add(Saved, R);
			Account->ServerEconomy(R);
		}
		else if (Op == TEXT("progression_ui") && Controller)
		{
			TArray<UUserWidget*> Widgets;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, ULobbyWidget::StaticClass(), true);
			bool Tasks = false;
			Command->TryGetBoolField(TEXT("tasks"), Tasks);
			if (Widgets.Num() == 1)
				if (auto* Button = Cast<UButton>(
					Widgets[0]->GetWidgetFromName(Tasks ? TEXT("QuestsButton") : TEXT("ShopButton"))))
					Button->OnClicked
					      .Broadcast();
		}
		else if (Op == TEXT("unlock") && PC && Interaction && Interaction->Inventory())
		{
			int32 Revision = Interaction->Inventory()->Contents.Revision;
			Command->TryGetNumberField(TEXT("revision"), Revision);
			for (TActorIterator<AExtractionKeyRoom> It(World); It; ++It)
			{
				PC->ServerUnlockDoor(*It, Revision);
				break;
			}
		}
		else if (Op == TEXT("deadline_tie") && Role == TEXT("Server"))
		{
			if (auto* GS = World->GetGameState<AExtractionGameState>())
				for (APlayerState* State : GS->PlayerArray)
					if (auto* PS = Cast<AExtractionPlayerState>(State); PS && PS->GetPlayerName() == Command->
						GetStringField(TEXT("target")))
						GS->Deadline = PS->ExtractAt;
		}
		else if (Op == TEXT("stash_fill") && Role == TEXT("Server"))
		{
			auto* Store = World->GetGameInstance()->GetSubsystem<UExtractionPersistence>();
			FGuid Id;
			FGuid::Parse(Command->GetStringField(TEXT("profile")), Id);
			auto Candidate = Store->State;
			if (auto* P = Candidate.Profiles.FindByPredicate([&](const auto& V) { return V.View.Profile == Id; }))
			{
				for (int32 N = 0; N < 120; ++N)
					if (!ExtractionItems::AutoPlace(
						P->View.Stash, ExtractionItems::Make(TEXT("Ammo"), 60)))
						break;
				++P->View.Revision;
				++P->View.Stash.Revision;
				if (!Store->Commit(Candidate)) Error = Store->Error;
			}
		}
		else if (Op == TEXT("lobby_button") && Controller)
		{
			TArray<UUserWidget*> Widgets;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, ULobbyWidget::StaticClass(), true);
			if (Widgets.Num() == 1)
				if (auto* Button = Cast<UButton>(
					Widgets[0]->GetWidgetFromName(FName(*Command->GetStringField(TEXT("name"))))))
					if (Button->GetIsEnabled()) Button->OnClicked.Broadcast();
		}
		else if (Op == TEXT("stash_ui") && Controller)
		{
			TArray<UUserWidget*> Widgets;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, ULobbyWidget::StaticClass(), true);
			if (Widgets.Num() == 1)
				if (auto* Button = Cast<
					UButton>(Widgets[0]->GetWidgetFromName(TEXT("StashButton"))))
					Button->OnClicked.Broadcast();
		}
		else if (Op == TEXT("storage") && Account)
		{
			bool Duplicate = false;
			Command->TryGetBoolField(TEXT("duplicate"), Duplicate);
			if (Duplicate)
			{
				Account->ServerMove(LastStorageRequest);
				return true;
			}
			FExtractionStorageRequest R;
			R.Operation = FGuid::NewGuid();
			R.Revision = Account->Profile.Revision;
			int32 From = 0, To = 1;
			Command->TryGetNumberField(TEXT("from"), From);
			Command->TryGetNumberField(TEXT("to"), To);
			R.From = static_cast<EExtractionStorageSide>(From);
			R.To = static_cast<EExtractionStorageSide>(To);
			FGuid::Parse(Command->GetStringField(TEXT("item")), R.Move.Item);
			Command->TryGetNumberField(TEXT("quantity"), R.Move.Quantity);
			Command->TryGetNumberField(TEXT("slot"), R.Move.Slot);
			Command->TryGetNumberField(TEXT("revision"), R.Revision);
			Command->TryGetNumberField(TEXT("x"), R.Move.Cell.X);
			Command->TryGetNumberField(TEXT("y"), R.Move.Cell.Y);
			Command->TryGetBoolField(TEXT("auto"), R.Move.AutoPlace);
			Command->TryGetBoolField(TEXT("rotated"), R.Move.Rotated);
			FString Merge;
			if (Command->TryGetStringField(TEXT("merge"), Merge)) FGuid::Parse(Merge, R.Move.MergeInto);
			EquipmentRequest(Command, R.Move);
			LastStorageRequest = R;
			Account->ServerMove(R);
		}
		else if (Op == TEXT("inventory") && PC)
			PC->RequestOpenInventory(nullptr, Command->GetBoolField(TEXT("open")));
		else if (Op == TEXT("menu") && PC) PC->ToggleExtractionMenu();
		else if (Op == TEXT("open_loot") && PC)
		{
			const FString Target = Command->GetStringField(TEXT("target"));
			for (TActorIterator<AExtractionLootActor> It(World); It; ++It)
				if (It->ContainerId.ToString() == Target || It->GetName() == Target)
				{
					PC->RequestOpenInventory(*It, true);
					return true;
				}
			Error = TEXT("Loot actor not visible to client");
		}
		else if (Op == TEXT("medical") && PC) PC->UseMedical();
		else if (Op == TEXT("cancel_medical") && PC) PC->ServerCancelMedical();
		else if (Op == TEXT("swap") && PC) PC->ServerSwapExtractionWeapons();
		else if (Op == TEXT("occluder") && Role == TEXT("Server"))
		{
			if (TestOccluder.IsValid()) TestOccluder->Destroy();
			if (Command->GetBoolField(TEXT("enabled")))
			{
				auto* Wall = World->SpawnActor<AActor>();
				auto* Box = NewObject<UBoxComponent>(Wall);
				Wall->SetRootComponent(Box);
				Box->SetBoxExtent(FVector(5, 60, 130));
				Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Box->SetCollisionResponseToAllChannels(ECR_Block);
				Box->RegisterComponent();
				FVector Position(1450, 4200, 490);
				const TArray<TSharedPtr<FJsonValue>>* Coordinates = nullptr;
				if (Command->TryGetArrayField(TEXT("position"), Coordinates) && Coordinates->Num() == 3)
					Position = FVector((*Coordinates)[0]->AsNumber(), (*Coordinates)[1]->AsNumber(),
					                   (*Coordinates)[2]->AsNumber());
				Wall->SetActorLocation(Position);
				TestOccluder = Wall;
			}
		}
		else if (Op == TEXT("item") && Interaction && Interaction->Inventory())
		{
			bool Duplicate = false;
			Command->TryGetBoolField(TEXT("duplicate"), Duplicate);
			if (Duplicate)
			{
				PC->ServerItemOperation(LastItemRequest);
				return true;
			}
			FExtractionItemRequest R;
			R.Sequence = ++PC->LocalItemSequence;
			R.Session = Interaction->Session;
			R.InventoryRevision = Interaction->Inventory()->Contents.Revision;
			R.LootRevision = Interaction->LootView.Revision;
			FGuid::Parse(Command->GetStringField(TEXT("item")), R.Item);
			FString Kind;
			Command->TryGetStringField(TEXT("kind"), Kind);
			R.Op = Kind == TEXT("drop")
				       ? EExtractionItemOp::Drop
				       : Kind == TEXT("use")
				       ? EExtractionItemOp::Use
				       : EExtractionItemOp::Transfer;
			Command->TryGetBoolField(TEXT("from_loot"), R.FromLoot);
			Command->TryGetBoolField(TEXT("to_loot"), R.ToLoot);
			Command->TryGetBoolField(TEXT("auto"), R.AutoPlace);
			Command->TryGetBoolField(TEXT("rotated"), R.Rotated);
			Command->TryGetNumberField(TEXT("quantity"), R.Quantity);
			Command->TryGetNumberField(TEXT("slot"), R.Slot);
			Command->TryGetNumberField(TEXT("x"), R.Cell.X);
			Command->TryGetNumberField(TEXT("y"), R.Cell.Y);
			Command->TryGetNumberField(TEXT("inventory_revision"), R.InventoryRevision);
			Command->TryGetNumberField(TEXT("loot_revision"), R.LootRevision);
			FString Merge;
			if (Command->TryGetStringField(TEXT("merge"), Merge)) FGuid::Parse(Merge, R.MergeInto);
			EquipmentRequest(Command, R);
			LastItemRequest = R;
			PC->ServerItemOperation(R);
		}
		else if (Op == TEXT("spawn_loot") && Role == TEXT("Server"))
		{
			const auto& P = Command->GetArrayField(TEXT("position"));
			bool Loose = false;
			Command->TryGetBoolField(TEXT("loose"), Loose);
			const FName Def(*Command->GetStringField(TEXT("definition")));
			auto* Loot = AExtractionLootActor::SpawnContainer(
				World, FVector(P[0]->AsNumber(), P[1]->AsNumber(), P[2]->AsNumber()), Loose, Def);
			if (!Loot)
			{
				Error = TEXT("Cannot create loot fixture");
				return true;
			}
			Loot->Label = Command->GetStringField(TEXT("label"));
			FExtractionContainer Items;
			Items.Id = FGuid::NewGuid();
			int32 Count = 1, Quantity = 1;
			Command->TryGetNumberField(TEXT("count"), Count);
			Command->TryGetNumberField(TEXT("quantity"), Quantity);
			for (int32 Index = 0; Index < Count; ++Index)
				ExtractionItems::AutoPlace(
					Items, ExtractionItems::Make(Def, Quantity));
			Loot->SetContents(Items);
		}
		else if (Op == TEXT("inventory_fixture") && Role == TEXT("Server"))
		{
			auto* Pawn = FindPawn(World, Command->GetStringField(TEXT("target")));
			auto* Inv = Pawn ? Pawn->GetExtractionInteraction()->Inventory() : nullptr;
			if (!Inv)
			{
				Error = TEXT("No fixture inventory");
				return true;
			}
			bool Clear = false;
			Command->TryGetBoolField(TEXT("clear"), Clear);
			if (Clear) Inv->Contents.Items.Reset();
			for (const auto& Value : Command->GetArrayField(TEXT("items")))
			{
				const auto Entry = Value->AsObject();
				int32 Count = 1, Quantity = 1, Slot = -1;
				Entry->TryGetNumberField(TEXT("count"), Count);
				Entry->TryGetNumberField(TEXT("quantity"), Quantity);
				Entry->TryGetNumberField(TEXT("slot"), Slot);
				for (int32 Index = 0; Index < Count; ++Index)
				{
					auto Item = ExtractionItems::Make(FName(*Entry->GetStringField(TEXT("definition"))), Quantity);
					Item.Slot = Slot;
					if (Slot >= 0) Inv->Contents.Items.Add(Item);
					else if (!ExtractionItems::AutoPlace(Inv->Contents, Item)) Error = TEXT("Fixture has no space");
				}
			}
			++Inv->Contents.Revision;
			Inv->SyncWeapons();
			Inv->Changed();
		}
		else if (Op == TEXT("mouse") && Controller && World->GetGameViewport())
		{
			const auto Viewport = World->GetGameViewport()->GetGameViewportWidget();
			if (!Viewport.IsValid()) return true;
			const FGeometry Geometry = Viewport->GetCachedGeometry();
			const float UIScale = FMath::Min(Geometry.GetLocalSize().X / 1280, Geometry.GetLocalSize().Y / 720);
			const FVector2D UIOffset = (Geometry.GetLocalSize() - FVector2D(1280, 720) * UIScale) * .5f;
			const FVector2D P = Geometry.LocalToAbsolute(UIOffset + FVector2D(
				Command->GetNumberField(TEXT("x")), Command->GetNumberField(TEXT("y"))) * UIScale);
			FString Action, Button;
			Command->TryGetStringField(TEXT("action"), Action);
			Command->TryGetStringField(TEXT("button"), Button);
			const FKey Key = Button == TEXT("right") ? EKeys::RightMouseButton : EKeys::LeftMouseButton;
			if (Action == TEXT("down")) PressedMouse.Add(Key);
			if (Action == TEXT("up")) PressedMouse.Remove(Key);
			FPointerEvent Event(0, P, LastMouse, PressedMouse, Key, 0, FModifierKeysState());
			FSlateApplication::Get().SetCursorPos(P);
			FSlateApplication::Get().ProcessMouseMoveEvent(Event, false);
			if (Action == TEXT("down")) FSlateApplication::Get().ProcessMouseButtonDownEvent(nullptr, Event);
			if (Action == TEXT("up")) FSlateApplication::Get().ProcessMouseButtonUpEvent(Event);
			LastMouse = P;
		}
		else if (Op == TEXT("input_key") && Controller)
		{
			const FKey Key(*Command->GetStringField(TEXT("key")));
			const bool Pressed = Command->GetBoolField(TEXT("pressed"));
			Controller->InputKey(
				FInputKeyEventArgs::CreateSimulated(Key, Pressed ? IE_Pressed : IE_Released, Pressed ? 1.f : 0.f));
		}
		else if (Op == TEXT("key") && Controller)
		{
			const FKey Key(*Command->GetStringField(TEXT("key")));
			FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
			FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
		}
		else return false;
		return true;
	}

	static ABrokenEdgeCharacter* FindPawn(UWorld* World, const FString& Name)
	{
		if (!World->GetGameState()) return nullptr;
		for (APlayerState* PS : World->GetGameState()->PlayerArray)
			if (PS->GetPlayerName() == Name) return Cast<ABrokenEdgeCharacter>(PS->GetPawn());
		for (TActorIterator<ABrokenEdgeCharacter> It(World); It; ++It)
			if (It->bExtractionGuard && FString::Printf(
				TEXT("GUARD_%d"), It->ExtractionGuardId) == Name)
				return *It;
		return nullptr;
	}

	static void Inject(APlayerController* PC, const FString& Name, bool Pressed)
	{
		auto* Input = PC->GetLocalPlayer()
			              ? PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()
			              : nullptr;
		const FString ActionName = Name == TEXT("MoveBack") || Name == TEXT("MoveRight") ? TEXT("Move") : Name;
		const FString Path = ActionName == TEXT("Sprint")
			                     ? TEXT("/Game/Characters/Stinger/Blueprints/Input/IA_StingerSprint.IA_StingerSprint")
			                     : TEXT("/Game/Input/Actions/IA_") + ActionName + TEXT(".IA_") + ActionName;
		auto* Action = LoadObject<UInputAction>(nullptr, *Path);
		if (Input && Action)
			Input->InjectInputForAction(Action, ActionName == TEXT("Move")
				                                    ? FInputActionValue(FVector2D(
					                                    Pressed && Name == TEXT("MoveRight") ? 1 : 0,
					                                    Pressed && Name != TEXT("MoveRight")
						                                    ? (Name == TEXT("MoveBack") ? -1 : 1)
						                                    : 0))
				                                    : FInputActionValue(Pressed), {}, {});
	}

	static TArray<TSharedPtr<FJsonValue>> VectorJson(const FVector& P)
	{
		return {
			MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y), MakeShared<FJsonValueNumber>(P.Z)
		};
	}

	void WriteState(UWorld* World, APlayerController* PC)
	{
		auto State = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> VisibleWidgets;
		TArray<UUserWidget*> Widgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, UUserWidget::StaticClass(), true);
		for (auto* Widget : Widgets)
			if (Widget->IsInViewport() && Widget->IsVisible())
				VisibleWidgets.Add(MakeShared<FJsonValueString>(Widget->GetClass()->GetName()));
		State->SetArrayField(TEXT("visible_widgets"), VisibleWidgets);
		for (auto* Widget : Widgets)
			if (Cast<UExtractionWidget>(Widget) && Widget->IsInViewport())
			{
				auto Hud = MakeShared<FJsonObject>();
				if (const auto* Text = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("ExtractionCountdown"))))
					Hud->SetStringField(TEXT("countdown_text"), Text->GetText().ToString());
				if (const auto* Progress = Cast<UProgressBar>(Widget->GetWidgetFromName(TEXT("ExtractionProgress"))))
				{
					Hud->SetBoolField(TEXT("progress_visible"), Widget->IsVisible() && Progress->IsVisible());
					Hud->SetNumberField(TEXT("progress_percent"), Progress->GetPercent());
				}
				State->SetObjectField(TEXT("extraction_hud"), Hud);
			}
		int32 Showcases = 0;
		for (TActorIterator<ALobbyShowcase> It(World); It; ++It)
		{
			++Showcases;
			State->SetStringField(TEXT("lobby_weapon"), It->GetHeldDefinition().ToString());
		}
		State->SetNumberField(TEXT("lobby_showcases"), Showcases);
		State->SetStringField(TEXT("role"), Role);
		State->SetNumberField(TEXT("command"), LastCommand);
		State->SetNumberField(TEXT("walk_index"), WalkIndex);
		State->SetNumberField(TEXT("walk_count"), WalkPoints.Num());
		State->SetNumberField(TEXT("world_time"), World->GetTimeSeconds());
		State->SetNumberField(TEXT("frame_delta"), World->GetDeltaSeconds());
		const auto Memory = FPlatformMemory::GetStats();
		State->SetNumberField(TEXT("physical_mib"), Memory.UsedPhysical / 1048576.);
		State->SetNumberField(TEXT("peak_physical_mib"), Memory.PeakUsedPhysical / 1048576.);
		if (PC)
		{
			int32 Width = 0, Height = 0;
			PC->GetViewportSize(Width, Height);
			State->SetNumberField(TEXT("viewport_width"), Width);
			State->SetNumberField(TEXT("viewport_height"), Height);
			FVector Camera;
			FRotator Rotation;
			PC->GetPlayerViewPoint(Camera, Rotation);
			State->SetArrayField(TEXT("view_position"), VectorJson(Camera));
		}
		auto Quality = MakeShared<FJsonObject>();
		for (const TCHAR* Name : {
			     TEXT("sg.ViewDistanceQuality"), TEXT("sg.AntiAliasingQuality"), TEXT("sg.ShadowQuality"),
			     TEXT("sg.GlobalIlluminationQuality"), TEXT("sg.ReflectionQuality"), TEXT("sg.PostProcessQuality"),
			     TEXT("sg.TextureQuality"), TEXT("sg.EffectsQuality"), TEXT("sg.FoliageQuality"),
			     TEXT("sg.ShadingQuality"),
			     TEXT("r.ScreenPercentage"), TEXT("r.VSync"), TEXT("t.MaxFPS")
		     })
			if (const auto* Value = IConsoleManager::Get().FindConsoleVariable(Name))
				Quality->SetNumberField(
					Name, Value->GetFloat());
		State->SetObjectField(TEXT("quality"), Quality);
		State->SetStringField(TEXT("error"), Error);
		State->SetStringField(TEXT("map"), World->GetPackage()->GetName());
		State->SetNumberField(TEXT("net_mode"), World->GetNetMode());
		State->SetNumberField(TEXT("local_players"), World->GetGameInstance()->GetNumLocalPlayers());
		if (const auto* Region = AExtractionRegionStream::Find(World))
		{
			auto Entry = MakeShared<FJsonObject>();
			Entry->SetBoolField(TEXT("requested"), Region->IsRequested());
			Entry->SetBoolField(TEXT("loaded"), Region->IsLoaded());
			Entry->SetBoolField(TEXT("visible"), Region->IsReady());
			Entry->SetBoolField(TEXT("blocked"), Region->IsBlocked());
			Entry->SetNumberField(TEXT("loads"), Region->GetLoads());
			Entry->SetNumberField(TEXT("unloads"), Region->GetUnloads());
			Entry->SetStringField(TEXT("status"), Region->GetStatus());
			State->SetObjectField(TEXT("region"), Entry);
		}
		if (const UNetDriver* Driver = World->GetNetDriver())
		{
			State->SetNumberField(TEXT("packet_lag"), Driver->PacketSimulationSettings.PktLag);
			State->SetNumberField(TEXT("packet_loss"), Driver->PacketSimulationSettings.PktLoss);
			State->SetNumberField(TEXT("net_in_bytes_per_second"), Driver->InBytesPerSecond);
			State->SetNumberField(TEXT("net_out_bytes_per_second"), Driver->OutBytesPerSecond);
		}
		if (auto* Mode = World->GetAuthGameMode())
		{
			State->SetStringField(TEXT("game_mode"), Mode->GetClass()->GetPathName());
			State->SetNumberField(TEXT("capacity"), Mode->GameSession->MaxPlayers);
		}
		if (auto* GS = World->GetGameState<AExtractionGameState>())
		{
			State->SetStringField(TEXT("storage_error"), GS->StorageError);
			TArray<TSharedPtr<FJsonValue>> ExitStates;
			for (TActorIterator<AExtractionExit> It(World); It; ++It)
			{
				auto Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("id"), It->Definition.Id.ToString());
				Entry->SetStringField(TEXT("label"), It->Definition.Label.ToString());
				Entry->SetArrayField(TEXT("bounds_center"), VectorJson(It->GetActorTransform().TransformPosition(It->Definition.BoundsOffset)));
				Entry->SetArrayField(TEXT("half_extent"), VectorJson(It->Definition.HalfExtent));
				Entry->SetArrayField(TEXT("position"), VectorJson(It->GetActorLocation()));
				Entry->SetNumberField(TEXT("opens_after"), It->Definition.OpensAfterSeconds);
				Entry->SetNumberField(TEXT("closes_after"), It->Definition.ClosesAfterSeconds);
				Entry->SetNumberField(TEXT("round_started"), It->RoundStartedAt);
				Entry->SetNumberField(TEXT("hold_seconds"), It->Definition.HoldSeconds);
				Entry->SetNumberField(TEXT("capacity"), It->Definition.Capacity);
				Entry->SetNumberField(TEXT("used"), It->UsedCapacity);
				Entry->SetBoolField(TEXT("available"), It->Available(GS->GetServerWorldTimeSeconds()));
				Entry->SetStringField(TEXT("reason"), It->UnavailableReason(GS->GetServerWorldTimeSeconds()));
				ExitStates.Add(MakeShared<FJsonValueObject>(Entry));
				State->SetArrayField(TEXT("exit_position"), VectorJson(It->GetActorLocation()));
				FHitResult Floor;
				if (World->LineTraceSingleByChannel(Floor, FVector(1400, 2900, 650), FVector(1400, 2900, -1000),
				                                    ECC_Visibility))
				{
					State->SetArrayField(TEXT("exit_trace"), VectorJson(Floor.ImpactPoint));
					State->SetStringField(TEXT("exit_surface"), GetNameSafe(Floor.GetActor()));
				}
			}
			State->SetArrayField(TEXT("exits"), ExitStates);
			State->SetNumberField(TEXT("phase"), static_cast<int32>(GS->Phase));
			State->SetNumberField(TEXT("alive"), GS->AlivePlayers);
			State->SetNumberField(TEXT("remaining"), GS->Deadline - GS->GetServerWorldTimeSeconds());
		}
		if (auto* Sessions = World->GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>())
			State->SetBoolField(TEXT("session"), Sessions->HasSession());
		if (Role == TEXT("Server"))
		{
			TArray<TSharedPtr<FJsonValue>> Results;
			for (const auto& Stored : World->GetGameInstance()->GetSubsystem<UExtractionPersistence>()->TestState().
			                                 Profiles)
			{
				auto Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("profile"), Stored.View.Profile.ToString());
				Result->SetNumberField(TEXT("outcome"), static_cast<int32>(Stored.View.Result.Outcome));
				Result->SetObjectField(TEXT("progression"), ProgressionJson(Stored.View.Progression));
				Result->SetNumberField(TEXT("economy_receipts"), Stored.EconomyReceipts.Num());
				Results.Add(MakeShared<FJsonValueObject>(Result));
			}
			State->SetArrayField(TEXT("stored_results"), Results);
		}
		if (auto* ExtractPC = Cast<AExtractionPlayerController>(PC))
		{
			State->SetBoolField(TEXT("spectating"), ExtractPC->bExtractionSpectating);
			State->SetStringField(TEXT("spectated"), ExtractPC->SpectatedPlayer
				                                         ? ExtractPC->SpectatedPlayer->GetPlayerName()
				                                         : TEXT(""));
		}
		if (PC)
		{
			if (const auto* Account = PC->FindComponentByClass<UExtractionAccountComponent>())
				State->SetObjectField(
					TEXT("account"), AccountJson(Account));
			State->SetBoolField(TEXT("has_pawn"), PC->GetPawn() != nullptr);
			int32 Width, Height;
			PC->GetViewportSize(Width, Height);
			State->SetNumberField(TEXT("width"), Width);
			State->SetNumberField(TEXT("height"), Height);
		}
		TArray<TSharedPtr<FJsonValue>> Players;
		if (const auto* ExtractionPC = Cast<AExtractionPlayerController>(PC))
		{
			State->SetStringField(TEXT("item_message"), ExtractionPC->ItemMessage);
			State->SetNumberField(TEXT("item_sequence"), ExtractionPC->ItemResultSequence);
		}
		if (World->GetGameState())
			for (APlayerState* PS : World->GetGameState()->PlayerArray)
			{
				auto Player = MakeShared<FJsonObject>();
				Player->SetStringField(TEXT("name"), PS->GetPlayerName());
				Player->SetNumberField(TEXT("id"), PS->GetPlayerId());
				if (const auto* OwnerPC = Cast<APlayerController>(PS->GetOwner()))
					if (const auto* Account = OwnerPC->FindComponentByClass<UExtractionAccountComponent>())
						Player->
							SetObjectField(TEXT("account"), AccountJson(Account));
				if (const auto* LobbyPS = Cast<ABrokenEdgePlayerState>(PS))
				{
					Player->SetBoolField(TEXT("ready"), LobbyPS->IsLobbyReady());
					Player->SetNumberField(TEXT("squad"), LobbyPS->ExtractionSquad);
					Player->SetStringField(TEXT("extraction_raid"), LobbyPS->ExtractionRaid.ToString());
				}
				if (auto* ExtractionPS = Cast<AExtractionPlayerState>(PS))
				{
					Player->SetStringField(TEXT("extract_exit_id"), ExtractionPS->ExtractExitId.ToString());
					Player->SetStringField(TEXT("extract_blocked_reason"), ExtractionPS->ExtractBlockedReason);
					Player->SetNumberField(TEXT("extract_at"), ExtractionPS->ExtractAt);
					Player->SetNumberField(
						TEXT("life"), static_cast<int32>(ExtractionPS->Life));
					Player->SetObjectField(TEXT("inventory"), InventoryJson(ExtractionPS->Inventory->Contents));
					Player->SetNumberField(TEXT("extract_at"), ExtractionPS->ExtractAt);
					Player->SetNumberField(TEXT("down_health"), ExtractionPS->DownHealth);
					Player->SetNumberField(TEXT("bleed_at"), ExtractionPS->BleedAt);
					Player->SetNumberField(TEXT("guard_kills"), ExtractionPS->RaidProgress.GuardKills);
				}
				if (auto* Character = Cast<ABrokenEdgeCharacter>(PS->GetPawn()))
				{
					Player->SetStringField(TEXT("pawn_name"), Character->GetName());
					Player->SetArrayField(TEXT("view_position"), VectorJson(Character->GetPawnViewLocation()));
					Player->SetNumberField(TEXT("health"), Character->GetHealth());
					Player->SetBoolField(TEXT("knife_active"), Character->Melee->Active);
					Player->SetBoolField(TEXT("knife_view_attached"), Character->Melee->IsViewAttached());
					Player->SetBoolField(TEXT("bleeding"), Character->GetExtractionHealth()->Bleeding);
					Player->SetBoolField(TEXT("fracture"), Character->GetExtractionHealth()->LegFracture);
					Player->SetBoolField(TEXT("painkiller"), Character->GetExtractionHealth()->Painkiller);
					Player->SetNumberField(
						TEXT("bullet_sequence"), Character->GetExtractionHealth()->TestBulletSequence);
					Player->SetStringField(TEXT("hit_bone"), Character->GetExtractionHealth()->TestHitBone.ToString());
					Player->SetNumberField(TEXT("hit_loss"), Character->GetExtractionHealth()->TestHealthLoss);
					Player->SetNumberField(TEXT("walk_speed"), Character->GetCharacterMovement()->MaxWalkSpeed);
					Player->SetNumberField(
						TEXT("crouch_speed"), Character->GetCharacterMovement()->MaxWalkSpeedCrouched);
					const auto* Ability = Character->GetExtractionAbilities();
					Player->SetNumberField(TEXT("action"), static_cast<int32>(Ability->Action));
					Player->SetNumberField(TEXT("action_end"), Ability->ActionEnd);
					Player->SetNumberField(TEXT("heal_ready"), Ability->HealReadyAt);
					Player->SetNumberField(TEXT("smoke_ready"), Ability->SmokeReadyAt);
					Player->SetStringField(TEXT("rescuer"), Ability->Rescuer && Ability->Rescuer->GetPlayerState()
						                                        ? Ability->Rescuer->GetPlayerState()->GetPlayerName()
						                                        : TEXT(""));
					const auto* Interaction = Character->GetExtractionInteraction();
					TArray<TSharedPtr<FJsonValue>> Reach;
					for (TActorIterator<AExtractionLootActor> It(World); It; ++It)
					{
						const float Distance = FVector::Dist(Character->GetActorLocation(), It->InteractionPoint());
						if (Distance > 220 || It->Empty) continue;
						FVector Eye;
						FRotator Rotation;
						Character->GetActorEyesViewPoint(Eye, Rotation);
						FCollisionQueryParams Query(SCENE_QUERY_STAT(ExtractionTestReach), false, Character);
						if (Character->GetEquippedWeapon()) Query.AddIgnoredActor(Character->GetEquippedWeapon());
						FHitResult Hit;
						World->LineTraceSingleByChannel(Hit, Eye, It->InteractionPoint(), ECC_Visibility, Query);
						auto Entry = MakeShared<FJsonObject>();
						Entry->SetStringField(TEXT("target"), It->ContainerId.ToString());
						Entry->SetStringField(TEXT("hit"), GetNameSafe(Hit.GetActor()));
						Entry->SetStringField(TEXT("name"), It->GetName());
						Entry->SetNumberField(TEXT("distance"), Distance);
						Entry->SetBoolField(TEXT("clear"), !Hit.bBlockingHit || Hit.GetActor() == *It);
						Entry->SetArrayField(TEXT("eye"), VectorJson(Eye));
						Reach.Add(MakeShared<FJsonValueObject>(Entry));
					}
					Player->SetArrayField(TEXT("reach"), Reach);
					Player->SetBoolField(TEXT("inventory_open"), Interaction->InventoryOpen);
					Player->SetStringField(TEXT("focus"), GetNameSafe(Interaction->Focus));
					Player->SetStringField(TEXT("loot_target"), GetNameSafe(Interaction->Loot));
					Player->SetStringField(
						TEXT("loot_id"), Interaction->Loot ? Interaction->Loot->ContainerId.ToString() : TEXT(""));
					Player->SetObjectField(TEXT("loot_view"), InventoryJson(Interaction->LootView));
					Player->SetNumberField(TEXT("session"), Interaction->Session);
					Player->SetNumberField(TEXT("medical_end"), Interaction->MedicalEnd);
					Player->SetNumberField(TEXT("search_end"), Interaction->SearchEnd);
					Player->SetNumberField(TEXT("shield"), Character->GetShield());
					Player->SetBoolField(TEXT("disabled"), Character->bDisableGameplay);
					Player->SetBoolField(TEXT("defeated"), Character->IsDefeated());
					Player->SetBoolField(TEXT("first_person"), Character->IsFirstPersonView());
					Player->SetBoolField(TEXT("sprinting"), Character->IsSprinting());
					Player->SetStringField(TEXT("pawn_class"), Character->GetClass()->GetPathName());
					if (const auto* Movement = Cast<UTraversalMovementComponent>(Character->GetCharacterMovement()))
					{
						const auto& Traversal = Character->GetLocalRole() == ROLE_SimulatedProxy
							                        ? Character->GetTraversal()->VisualState
							                        : Movement->State;
						Player->SetNumberField(TEXT("traversal"), static_cast<int32>(Traversal.Kind));
						Player->SetNumberField(TEXT("traversal_serial"), Traversal.Serial);
						Player->SetNumberField(TEXT("traversal_elapsed"), Traversal.Elapsed);
						Player->SetNumberField(TEXT("traversal_duration"), Traversal.Duration);
						Player->SetArrayField(TEXT("traversal_contact"), VectorJson(Traversal.Contact));
						Player->SetArrayField(TEXT("traversal_end"), VectorJson(Traversal.End));
						Player->SetStringField(TEXT("traversal_rejection"), Movement->LastRejection);
						Player->SetStringField(TEXT("ladder_focus"), GetNameSafe(Movement->FindLadder()));
						Player->SetStringField(TEXT("traversal_ladder"), GetNameSafe(Traversal.Ladder));
						Player->SetStringField(
							TEXT("ladder_reentry_block"), GetNameSafe(Traversal.ReattachBlockedLadder));
						Player->SetStringField(TEXT("traversal_prompt"), Character->GetTraversal()->Prompt());
						Player->SetBoolField(TEXT("crouched"), Character->bIsCrouched);
						const auto* OwnerPC = Cast<AExtractionPlayerController>(Character->Controller);
						Player->SetBoolField(TEXT("menu_open"), OwnerPC && OwnerPC->IsMenuOpen());
						Player->SetNumberField(
							TEXT("capsule_half"), Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
						Player->SetNumberField(
							TEXT("capsule_radius"), Character->GetCapsuleComponent()->GetScaledCapsuleRadius());
					}
					Player->SetStringField(TEXT("body_anim"), GetPathNameSafe(Character->GetMesh()->GetAnimInstance()));
					if (auto* BodyAnim = Character->GetMesh()->GetAnimInstance())
						Player->SetBoolField(
							TEXT("body_montage_active"), BodyAnim->GetCurrentActiveMontage() != nullptr);
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
					const auto* Mechanics = Character->GetExtractionCombat();
					Player->SetNumberField(TEXT("mechanics_revision"), Mechanics->Stats.Revision);
					Player->SetNumberField(TEXT("mechanics_speed"), Mechanics->Stats.Speed);
					Player->SetNumberField(TEXT("aim_alpha"), Mechanics->AimAlpha);
					Player->SetNumberField(TEXT("breath"), Mechanics->Breath);
					Player->SetNumberField(TEXT("burst_index"), Mechanics->BurstIndex);
					Player->SetNumberField(TEXT("spread_x"), Mechanics->SpreadAccumulation.X);
					Player->SetBoolField(TEXT("mechanics_ready"), Mechanics->CanFireMechanics());

					Player->SetNumberField(TEXT("rejected"), Character->GetExtractionCombat()->GetRejectedShots());
					Player->SetNumberField(TEXT("combat_state"), static_cast<int32>(Character->GetCombatState()));
					Player->SetNumberField(TEXT("carried_ammo"), Character->GetCombat()->GetCarriedAmmo());
					const FVector P = Character->GetActorLocation();
					Player->SetBoolField(TEXT("falling"), Character->GetCharacterMovement()->IsFalling());
					Player->SetArrayField(TEXT("position"), {
						                      MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y),
						                      MakeShared<FJsonValueNumber>(P.Z)
					                      });
					if (auto* Weapon = Character->GetEquippedWeapon())
					{
						Player->SetBoolField(TEXT("weapon_hidden"), Weapon->IsHidden());
						if (auto* ViewWeapon = Character->GetFirstPersonWeaponMesh())
							Player->SetBoolField(TEXT("fp_weapon_visible"), ViewWeapon->IsVisible());
						Player->SetNumberField(
							TEXT("left_grip_distance"),
							FVector::Dist(Character->GetMesh()->GetSocketLocation(TEXT("LeftHand")),
							              (Weapon->EquipmentGrip(false) * Weapon->GetWeaponMesh()->
								              GetComponentTransform()).GetLocation()));
						Player->SetNumberField(
							TEXT("right_grip_distance"),
							FVector::Dist(Character->GetMesh()->GetSocketLocation(TEXT("RightHand")),
							              Weapon->GetWeaponMesh()->GetSocketLocation(TEXT("Hand_R"))));
						Player->SetStringField(TEXT("weapon"), Weapon->GetName());
						Player->SetStringField(TEXT("weapon_item"), Weapon->ExtractionItemId.ToString());
						if (Character->IsLocallyControlled() && Character->GetFirstPersonMesh())
							Player->SetNumberField(TEXT("fp_left_grip_distance"), FVector::Dist(
								                       Character->GetFirstPersonMesh()->GetSocketLocation(
									                       TEXT("LeftHand")),
								                       Character->GetFirstPersonGripTarget(TEXT("LeftHandSocket")).
								                                  GetLocation()));
						TArray<TSharedPtr<FJsonValue>> Visuals;
						TInlineComponentArray<UStaticMeshComponent*> MeshParts(Weapon);
						for (const auto* Part : MeshParts)
						{
							auto Visual = MakeShared<FJsonObject>();
							Visual->SetStringField(TEXT("mesh"), GetPathNameSafe(Part->GetStaticMesh()));
							Visual->SetBoolField(TEXT("visible"), Part->IsVisible());
							Visual->SetBoolField(TEXT("owner_only"), Part->bOnlyOwnerSee);
							Visual->SetBoolField(TEXT("owner_hidden"), Part->bOwnerNoSee);
							Visuals.Add(MakeShared<FJsonValueObject>(Visual));
						}
						Player->SetArrayField(TEXT("equipment_visuals"), Visuals);
						Player->SetNumberField(TEXT("ammo"), Weapon->GetAmmo());
						Player->SetStringField(TEXT("ammo_type"), Weapon->ExtractionAmmoType.ToString());
						TArray<TSharedPtr<FJsonValue>> Parts;
						for (FName Part : Weapon->ExtractionParts)
							Parts.Add(
								MakeShared<FJsonValueString>(Part.ToString()));
						Player->SetArrayField(TEXT("weapon_parts"), Parts);
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
		TArray<TSharedPtr<FJsonValue>> LootActors;
		for (TActorIterator<AExtractionLootActor> It(World); It; ++It)
		{
			auto Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), It->GetName());
			Entry->SetStringField(TEXT("label"), It->Label);
			Entry->SetStringField(TEXT("source_point"), It->SourcePointId.ToString());
			Entry->SetStringField(TEXT("id"), It->ContainerId.ToString());
			Entry->SetBoolField(TEXT("loose"), It->Loose);
			Entry->SetBoolField(TEXT("empty"), It->Empty);
			Entry->SetStringField(TEXT("definition"), It->LooseDefinition.ToString());
			const FVector P = It->GetActorLocation();
			Entry->SetArrayField(TEXT("position"), {
				                     MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y),
				                     MakeShared<FJsonValueNumber>(P.Z)
			                     });
			Entry->SetObjectField(TEXT("contents"), InventoryJson(It->Contents));
			LootActors.Add(MakeShared<FJsonValueObject>(Entry));
		}
		State->SetArrayField(TEXT("loot"), LootActors);
		const auto* GuardRules = GetDefault<UExtractionRules>();
		auto RulesJson = MakeShared<FJsonObject>();
		RulesJson->SetNumberField(TEXT("reaction_seconds"), GuardRules->GuardReactionSeconds);
		RulesJson->SetNumberField(TEXT("shot_interval"), GuardRules->GuardShotInterval);
		RulesJson->SetNumberField(TEXT("damage_scale"), GuardRules->GuardDamageScale);
		State->SetObjectField(TEXT("guard_rules"), RulesJson);
		TArray<TSharedPtr<FJsonValue>> Guards, Smokes;
		for (TActorIterator<ABrokenEdgeCharacter> It(World); It; ++It)
			if (It->bExtractionGuard)
			{
				auto J = MakeShared<FJsonObject>();
				J->SetStringField(TEXT("name"), FString::Printf(TEXT("GUARD_%d"), It->ExtractionGuardId));
				J->SetStringField(TEXT("actor"), It->GetName());
				J->SetArrayField(TEXT("position"), VectorJson(It->GetActorLocation()));
				J->SetNumberField(TEXT("health"), It->GetHealth());
				J->SetBoolField(TEXT("dead"), It->IsDefeated());
				J->SetArrayField(TEXT("view_position"), VectorJson(It->GetPawnViewLocation()));
				J->SetNumberField(TEXT("combat_state"), static_cast<int32>(It->GetCombatState()));
				J->SetNumberField(TEXT("carried_ammo"), It->GetCombat()->GetCarriedAmmo());
				J->SetNumberField(TEXT("shots"), It->GetExtractionCombat()->GetAcceptedShots());
				if (const auto* Weapon = It->GetEquippedWeapon())
				{
					J->SetNumberField(TEXT("ammo"), Weapon->GetAmmo());
					J->SetNumberField(TEXT("fire_delay"), Weapon->FireDelay);
					J->SetNumberField(TEXT("damage"), Weapon->GetDamage());
					J->SetNumberField(TEXT("head_damage"), Weapon->GetHeadShotDamage());
				}
				TArray<TSharedPtr<FJsonValue>> ShotTimes;
				for (const auto& Shot : It->GetExtractionCombat()->GetTestShotTimes())
				{
					auto Entry = MakeShared<FJsonObject>();
					Entry->SetNumberField(TEXT("sequence"), Shot.Key);
					Entry->SetNumberField(TEXT("time"), Shot.Value);
					ShotTimes.Add(MakeShared<FJsonValueObject>(Entry));
				}
				J->SetArrayField(TEXT("shot_times"), ShotTimes);
				if (It->HasAuthority())
					J->SetObjectField(
						TEXT("inventory"), InventoryJson(It->GuardInventory->Contents));
				if (const auto* AI = Cast<AExtractionGuard>(It->GetController()))
				{
					if (const auto* PlayerA = FindPawn(World, TEXT("EX_A")))
					{
						J->SetBoolField(TEXT("line_of_sight_to_A"), AI->LineOfSightTo(PlayerA));
						J->SetBoolField(TEXT("smoke_blocked_to_A"), AExtractionSmoke::Blocks(
							World, It->GetPawnViewLocation(), PlayerA->GetPawnViewLocation()));
					}
					J->SetStringField(TEXT("route"), AI->Route ? AI->Route->GetActorNameOrLabel() : TEXT("None"));
					J->SetStringField(TEXT("route_object"), GetNameSafe(AI->Route));
					if (const auto* Blackboard = AI->GetBlackboardComponent())
					{
						J->SetNumberField(
							TEXT("reaction_ready_time"),
							Blackboard->GetValueAsFloat(GuardBlackboard::ReactionReadyTime));
						J->SetNumberField(
							TEXT("next_fire_time"), Blackboard->GetValueAsFloat(GuardBlackboard::NextFireTime));
					}
					J->SetNumberField(TEXT("state"), static_cast<int32>(AI->State));
					J->SetNumberField(TEXT("arrivals"), AI->PatrolArrivals);
					J->SetStringField(TEXT("target"), GetNameSafe(AI->Target));
				}
				Guards.Add(MakeShared<FJsonValueObject>(J));
			}
		for (TActorIterator<AExtractionSmoke> It(World); It; ++It)
		{
			auto J = MakeShared<FJsonObject>();
			J->SetArrayField(TEXT("position"), VectorJson(It->GetActorLocation()));
			J->SetNumberField(TEXT("radius"), It->Radius);
			J->SetNumberField(TEXT("expires"), It->ExpiresAt);
			// 实际写进材质的不透明度：逻辑上存在但 alpha 为 0 时玩家看到的是空气，
			// 只断言 actor 数量会漏掉这种情况（正是"烟直接消失"那个 bug）。
			J->SetNumberField(TEXT("alpha"), It->VisualAlpha());
			Smokes.Add(MakeShared<FJsonValueObject>(J));
		}
		TArray<TSharedPtr<FJsonValue>> Paths;
		if (Role == TEXT("Server"))
			for (TActorIterator<AExtractionPatrolRoute> It(World); It; ++It)
				for (int32 I = 0; I < It->Points.Num(); ++I)
				{
					auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
						World, It->Points[I], It->Points[(I + 1) % It->Points.Num()]);
					auto J = MakeShared<FJsonObject>();
					J->SetBoolField(TEXT("valid"), Path && Path->IsValid() && !Path->IsPartial());
					J->SetArrayField(TEXT("start"), VectorJson(It->Points[I]));
					J->SetArrayField(TEXT("end"), VectorJson(It->Points[(I + 1) % It->Points.Num()]));
					J->SetNumberField(TEXT("length"), Path ? Path->GetPathLength() : 0);
					Paths.Add(MakeShared<FJsonValueObject>(J));
				}
		State->SetArrayField(TEXT("nav_paths"), Paths);
		TArray<TSharedPtr<FJsonValue>> Rooms;
		for (TActorIterator<AExtractionKeyRoom> It(World); It; ++It)
		{
			auto J = MakeShared<FJsonObject>();
			J->SetBoolField(TEXT("unlocked"), It->Unlocked);
			J->SetBoolField(TEXT("open"), It->Open);
			J->SetArrayField(TEXT("position"), VectorJson(It->GetActorLocation()));
			J->SetArrayField(TEXT("approach"), VectorJson(It->Approach()));
			FHitResult Hit;
			const FVector Offset = It->GetActorRotation().RotateVector(FVector(0, 80, 0));
			J->SetBoolField(TEXT("blocked"), World->LineTraceSingleByChannel(
				                Hit, It->GetActorLocation() - Offset, It->GetActorLocation() + Offset, ECC_Visibility));
			if (It->Cache)
			{
				J->SetStringField(TEXT("cache"), It->Cache->ContainerId.ToString());
				J->SetArrayField(TEXT("cache_position"), VectorJson(It->Cache->GetActorLocation()));
				if (Role == TEXT("Server"))
				{
					auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
						World, It->Approach(), It->Cache->GetActorLocation() + FVector(0, 0, 70));
					J->SetBoolField(TEXT("nav_valid"), Path && Path->IsValid() && !Path->IsPartial());
				}
			}
			Rooms.Add(MakeShared<FJsonValueObject>(J));
		}
		State->SetArrayField(TEXT("key_rooms"), Rooms);
		State->SetArrayField(TEXT("guards"), Guards);
		State->SetArrayField(TEXT("smokes"), Smokes);
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
	if ((Role != TEXT("Server") && Role != TEXT("A") && Role != TEXT("B") && Role != TEXT("C") && Role != TEXT("D") &&
			Role != TEXT("E") && Role != TEXT("F") && Role != TEXT("G")) ||
		Folder.IsEmpty())
	{
		AddError(TEXT("Use Scripts/Extraction/run_network.py"));
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FExtractionDriver(this,Role,Folder));
	return true;
}
#endif
