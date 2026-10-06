#include "StingerAssetLibrary.h"
#include "Engine/GameViewportClient.h"
#include "Slate/SceneViewport.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "UObject/UnrealType.h"
#include "Editor.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "GameFramework/Pawn.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Serialization/JsonSerializer.h"
#include "Animation/Skeleton.h"
#include "Misc/App.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "AnimGraphNode_Fabrik.h"
#include "AnimGraphNode_WeaponGripReach.h"
#include "K2Node_VariableGet.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_Root.h"

USkeleton* UStingerAssetLibrary::CreateSourceSkeleton(const FString& PackageName, const TArray<FName>& Names,
                                                      const TArray<int32>& Parents,
                                                      const TArray<FTransform>& ReferencePose)
{
	if (Names.IsEmpty() || Names.Num() != Parents.Num() || Names.Num() != ReferencePose.Num()) return nullptr;
	UPackage* Package = CreatePackage(*PackageName);
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
	if (auto Existing = FindObject<USkeleton>(Package, *AssetName)) return Existing;
	auto Skeleton = NewObject<USkeleton>(Package, *AssetName, RF_Public | RF_Standalone);
	auto SourceMesh = NewObject<USkeletalMesh>();
	{
		FReferenceSkeletonModifier Modifier(SourceMesh->GetRefSkeleton(), nullptr);
		for (int32 Index = 0; Index < Names.Num(); ++Index)
		{
			const int32 Parent = Index == 0 ? INDEX_NONE : Parents[Index];
			if (Index > 0 && (Parent < 0 || Parent >= Index)) return nullptr;
			Modifier.Add(FMeshBoneInfo(Names[Index], Names[Index].ToString(), Parent), ReferencePose[Index]);
		}
	}
	if (!Skeleton->MergeAllBonesToBoneTree(SourceMesh, false)) return nullptr;
	FAssetRegistryModule::AssetCreated(Skeleton);
	Skeleton->MarkPackageDirty();
	return Skeleton;
}

bool UStingerAssetLibrary::AssignMeshSkeleton(USkeletalMesh* Mesh, USkeleton* Skeleton)
{
	if (!Mesh || !Skeleton || !Skeleton->IsCompatibleMesh(Mesh)) return false;
	Mesh->SetSkeleton(Skeleton);
	Mesh->SetPhysicsAsset(nullptr);
	Mesh->MarkPackageDirty();
	return true;
}

FString UStingerAssetLibrary::GetValidationState(APawn* Pawn)
{
	if (!Pawn) return TEXT("{}");
	// Read the state used by the real gameplay and animation paths.
	auto ObjectField = [](UObject* Object, FName Name) -> UObject*
	{
		if (!Object) return nullptr;
		auto Property = FindFProperty<FObjectPropertyBase>(Object->GetClass(), Name);
		return Property ? Property->GetObjectPropertyValue_InContainer(Object) : nullptr;
	};
	UObject* Combat = ObjectField(Pawn, TEXT("Combat"));
	UObject* Weapon = ObjectField(Combat, TEXT("EquippedWeapon"));
	TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
	UObject* OverlappingWeapon = ObjectField(Pawn, TEXT("OverlappingWeapon"));
	State->SetStringField(TEXT("overlapping_weapon"), OverlappingWeapon ? OverlappingWeapon->GetName() : TEXT(""));
	const auto Ammo = Weapon ? FindFProperty<FIntProperty>(Weapon->GetClass(), TEXT("Ammo")) : nullptr;
	State->SetNumberField(TEXT("ammo"), Ammo ? Ammo->GetPropertyValue_InContainer(Weapon) : -1);
	if (auto Property = Combat ? FindFProperty<FEnumProperty>(Combat->GetClass(), TEXT("CombatState")) : nullptr)
		State->SetStringField(TEXT("combat_state"), Property->GetEnum()->GetNameStringByValue(
			                      Property->GetUnderlyingProperty()->GetSignedIntPropertyValue(
				                      Property->ContainerPtrToValuePtr<void>(Combat))));
	if (auto Property = FindFProperty<FBoolProperty>(Pawn->GetClass(), TEXT("bDisableGameplay")))
		State->SetBoolField(TEXT("disabled"), Property->GetPropertyValue_InContainer(Pawn));
	if (auto Property = FindFProperty<FFloatProperty>(Pawn->GetClass(), TEXT("Health")))
		State->SetNumberField(TEXT("health"), Property->GetPropertyValue_InContainer(Pawn));
	if (auto Body = Cast<USkeletalMeshComponent>(ObjectField(Pawn, TEXT("Mesh"))))
	{
		State->SetStringField(
			TEXT("body_hand_actor_space"),
			Body->GetSocketTransform(TEXT("RightHand")).GetRelativeTransform(Pawn->GetActorTransform()).ToString());
		if (auto Anim = Body->GetAnimInstance())
			if (auto Active = FindFProperty<FBoolProperty>(Anim->GetClass(), TEXT("bUseFABRIK")))
				State->SetBoolField(TEXT("left_hand_ik_active"), Active->GetPropertyValue_InContainer(Anim));
	}
	if (auto WeaponActor = Cast<AActor>(Weapon))
		if (auto WeaponMesh = WeaponActor->FindComponentByClass<USkeletalMeshComponent>())
			State->SetStringField(
				TEXT("muzzle_actor_space"),
				Pawn->GetActorTransform().InverseTransformPosition(WeaponMesh->GetSocketLocation(TEXT("MuzzleFlash"))).
				      ToString());
	if (auto Mesh = Cast<USkeletalMeshComponent>(ObjectField(Pawn, TEXT("DedicatedFirstPersonMesh"))))
	{
		const FTransform Camera = CastChecked<UCameraComponent>(ObjectField(Pawn, TEXT("FollowCamera")))->
			GetComponentTransform();
		State->SetStringField(
			TEXT("fp_hand_camera_space"),
			Camera.InverseTransformPosition(Mesh->GetSocketLocation(TEXT("RightHand"))).ToString());
		State->SetStringField(
			TEXT("fp_root_camera_space"),
			Camera.InverseTransformPosition(Mesh->GetSocketLocation(TEXT("Root"))).ToString());
		State->SetStringField(
			TEXT("fp_head_camera_space"),
			Camera.InverseTransformPosition(Mesh->GetSocketLocation(TEXT("Head"))).ToString());
		State->SetStringField(
			TEXT("fp_hand_transform"),
			Mesh->GetSocketTransform(TEXT("RightHand")).GetRelativeTransform(Camera).ToString());
		State->SetStringField(
			TEXT("fp_gun_camera_space"),
			Mesh->GetSocketTransform(TEXT("b_IK_Gun")).GetRelativeTransform(Camera).ToString());
		if (auto ViewWeapon = Cast<USkeletalMeshComponent>(ObjectField(Pawn,TEXT("FirstPersonWeaponMesh"))))
		{
			State->SetNumberField(TEXT("right_grip_error"), FVector::Distance(
				                      Mesh->GetSocketLocation(TEXT("RightHand")),
				                      ViewWeapon->GetSocketLocation(TEXT("Hand_R"))));
			State->SetNumberField(TEXT("left_grip_error"), FVector::Distance(Mesh->GetSocketLocation(TEXT("LeftHand")),
			                                                                 ViewWeapon->GetSocketLocation(
				                                                                 TEXT("LeftHandSocket"))));
			State->SetStringField(
				TEXT("view_magazine"), ViewWeapon->GetSocketTransform(TEXT("b_gun_mag"), RTS_Component).ToString());
		}
		if (auto Anim = Mesh->GetAnimInstance())
			if (auto Montage = Anim->GetCurrentActiveMontage())
			{
				State->SetStringField(TEXT("view_montage"), Montage->GetPathName());
				State->SetNumberField(
					TEXT("view_montage_fraction"),
					Anim->Montage_GetPosition(Montage) / FMath::Max(.01f, Montage->GetPlayLength()));
			}
	}
	State->SetBoolField(TEXT("has_secondary_weapon"), ObjectField(Combat,TEXT("SecondaryWeapon")) != nullptr);
	FString Result;
	FJsonSerializer::Serialize(State, TJsonWriterFactory<>::Create(&Result));
	return Result;
}

FString UStingerAssetLibrary::DescribeThirdPersonGripIK(UAnimBlueprint* Blueprint)
{
	if (!Blueprint) return TEXT("{}");
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Nodes;
	for (auto Graph : Graphs)
		for (auto GraphNode : Graph->Nodes)
			if (auto Fabrik = Cast<UAnimGraphNode_Fabrik>(GraphNode))
			{
				const auto& Node = Fabrik->Node;
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("root"), Node.RootBone.BoneName.ToString());
				Entry->SetStringField(TEXT("tip"), Node.TipBone.BoneName.ToString());
				Entry->SetStringField(TEXT("effector_bone"), Node.EffectorTarget.BoneReference.BoneName.ToString());
				Entry->SetNumberField(TEXT("space"), Node.EffectorTransformSpace);
				Entry->SetNumberField(TEXT("rotation_source"), Node.EffectorRotationSource);
				Entry->SetNumberField(TEXT("precision"), Node.Precision);
				Entry->SetNumberField(TEXT("iterations"), Node.MaxIterations);
				Nodes.Add(MakeShared<FJsonValueObject>(Entry));
			}
	Result->SetArrayField(TEXT("fabrik"), Nodes);
	FString Text;
	FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Text));
	return Text;
}

bool UStingerAssetLibrary::ConfigureThirdPersonGripIK(UAnimBlueprint* Blueprint)
{
	if (!Blueprint) return false;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	TArray<UAnimGraphNode_Fabrik*> Nodes;
	for (auto Graph : Graphs)
		for (auto Node : Graph->Nodes)
			if (auto Fabrik = Cast<UAnimGraphNode_Fabrik>(Node)) Nodes.Add(Fabrik);
	// Only the Stinger arm graph is expected here; don't silently edit an unrelated chain.
	if (Nodes.Num() != 1) return false;
	UEdGraph* MainGraph = nullptr;
	UAnimGraphNode_Root* Root = nullptr;
	for (auto Graph : Graphs)
		if (Graph->GetFName() == TEXT("AnimGraph"))
		{
			MainGraph = Graph;
			for (auto GraphNode : Graph->Nodes)
				if (auto Candidate = Cast<UAnimGraphNode_Root>(GraphNode)) Root = Candidate;
		}
	if (!MainGraph || !Root) return false;
	auto Fabrik = Nodes[0];
	UAnimGraphNode_WeaponGripReach* Reach = nullptr;
	for (auto GraphNode : Fabrik->GetGraph()->Nodes)
		if (auto Existing = Cast<UAnimGraphNode_WeaponGripReach>(GraphNode)) Reach = Existing;
	if (Fabrik->GetGraph() != MainGraph)
	{
		// The old cached grip pose precedes WeaponSlot. Fire montages overwrite that result.
		// Bypass the old controls and solve once on the final animated pose instead.
		auto Input = (Reach ? Reach : static_cast<UAnimGraphNode_SkeletalControlBase*>(Fabrik))->FindPin(
			TEXT("ComponentPose"));
		auto Output = Fabrik->FindPin(TEXT("Pose"), EGPD_Output);
		if (!Input || Input->LinkedTo.Num() != 1 || !Output || Output->LinkedTo.IsEmpty()) return false;
		auto Previous = Input->LinkedTo[0];
		auto Consumers = Output->LinkedTo;
		for (auto Consumer : Consumers)
		{
			Consumer->BreakLinkTo(Output);
			Previous->MakeLinkTo(Consumer);
		}
		FBlueprintEditorUtils::RemoveNode(Blueprint, Fabrik, true);
		if (Reach) FBlueprintEditorUtils::RemoveNode(Blueprint, Reach, true);
		Fabrik = nullptr;
		Reach = nullptr;
	}
	if (!Fabrik)
	{
		auto Result = Root->FindPinChecked(TEXT("Result"));
		if (Result->LinkedTo.Num() != 1) return false;
		auto Previous = Result->LinkedTo[0];
		FGraphNodeCreator<UAnimGraphNode_LocalToComponentSpace> ToComponentCreator(*MainGraph);
		auto ToComponent = ToComponentCreator.CreateNode();
		ToComponentCreator.Finalize();
		FGraphNodeCreator<UAnimGraphNode_ComponentToLocalSpace> ToLocalCreator(*MainGraph);
		auto ToLocal = ToLocalCreator.CreateNode();
		ToLocalCreator.Finalize();
		FGraphNodeCreator<UAnimGraphNode_WeaponGripReach> ReachCreator(*MainGraph);
		Reach = ReachCreator.CreateNode();
		ReachCreator.Finalize();
		FGraphNodeCreator<UAnimGraphNode_Fabrik> FabrikCreator(*MainGraph);
		Fabrik = FabrikCreator.CreateNode();
		Fabrik->Node.AlphaInputType = EAnimAlphaInputType::Bool;
		Fabrik->Node.AlphaBoolBlend.BlendInTime = .1f;
		Fabrik->Node.AlphaBoolBlend.BlendOutTime = 0.f;
		FabrikCreator.Finalize();
		auto Variable = [&](FName Name, int32 Y)
		{
			FGraphNodeCreator<UK2Node_VariableGet> Creator(*MainGraph);
			auto Getter = Creator.CreateNode();
			Getter->VariableReference.SetSelfMember(Name);
			Getter->NodePosX = Root->NodePosX;
			Getter->NodePosY = Y;
			Creator.Finalize();
			return Getter->GetValuePin();
		};
		auto Target = Variable(TEXT("LeftHandTransform"), Root->NodePosY + 250);
		Target->MakeLinkTo(Fabrik->FindPinChecked(TEXT("EffectorTransform")));
		Target->MakeLinkTo(Reach->FindPinChecked(TEXT("LeftHandTarget")));
		Variable(TEXT("bUseFABRIK"), Root->NodePosY + 350)->MakeLinkTo(
			Fabrik->FindPinChecked(TEXT("bAlphaBoolEnabled")));
		Variable(TEXT("bUseGripReachCorrection"), Root->NodePosY + 450)->MakeLinkTo(
			Reach->FindPinChecked(TEXT("bEnabled")));
		Result->BreakLinkTo(Previous);
		Previous->MakeLinkTo(ToComponent->FindPinChecked(TEXT("LocalPose")));
		ToComponent->FindPinChecked(TEXT("ComponentPose"), EGPD_Output)->MakeLinkTo(
			Reach->FindPinChecked(TEXT("ComponentPose")));
		Reach->FindPinChecked(TEXT("Pose"), EGPD_Output)->MakeLinkTo(Fabrik->FindPinChecked(TEXT("ComponentPose")));
		Fabrik->FindPinChecked(TEXT("Pose"), EGPD_Output)->MakeLinkTo(ToLocal->FindPinChecked(TEXT("ComponentPose")));
		ToLocal->FindPinChecked(TEXT("Pose"), EGPD_Output)->MakeLinkTo(Result);
		int32 X = Root->NodePosX;
		const TArray<UEdGraphNode*> Controls = {ToComponent, Reach, Fabrik, ToLocal};
		for (auto GraphNode : Controls)
		{
			GraphNode->NodePosX = X;
			GraphNode->NodePosY = Root->NodePosY;
			X += 300;
		}
		Root->NodePosX = X;
	}
	if (!Reach) return false;
	auto& Node = Fabrik->Node;
	Node.RootBone.BoneName = TEXT("LeftArm");
	Node.TipBone.BoneName = TEXT("LeftHand");
	Node.EffectorTarget.bUseSocket = false;
	Node.EffectorTarget.BoneReference.BoneName = TEXT("RightHand");
	Node.EffectorTransformSpace = BCS_BoneSpace;
	Node.EffectorRotationSource = BRS_CopyFromTarget;
	Node.Precision = .1f;
	Node.MaxIterations = 24;
	Reach->Node.RightArm.BoneName = TEXT("RightArm");
	Reach->Node.RightForeArm.BoneName = TEXT("RightForeArm");
	Reach->Node.RightHand.BoneName = TEXT("RightHand");
	Reach->Node.LeftArm.BoneName = TEXT("LeftArm");
	Reach->Node.LeftForeArm.BoneName = TEXT("LeftForeArm");
	Reach->Node.LeftHand.BoneName = TEXT("LeftHand");
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return Blueprint->Status != BS_Error;
}

void UStingerAssetLibrary::SetValidationFixedStep(float Seconds)
{
	FApp::SetUseFixedTimeStep(Seconds > 0.f);
	if (Seconds > 0.f) FApp::SetFixedDeltaTime(Seconds);
}

AActor* UStingerAssetLibrary::SpawnValidationWeapon(UWorld* World, TSubclassOf<AActor> Class, FVector Location)
{
	if (!World || !Class) return nullptr;
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AActor>(Class, Location, FRotator::ZeroRotator, Parameters);
}

bool UStingerAssetLibrary::DropValidationWeapon(APawn* Pawn)
{
	auto Character = Cast<ABrokenEdgeCharacter>(Pawn);
	if (!Character || !Character->HasAuthority() || !Character->GetCombat()) return false;
	Character->GetCombat()->Drop();
	return true;
}

FString UStingerAssetLibrary::GetMontageDescription(UAnimMontage* Montage)
{
	if (!Montage) return TEXT("{}");
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("duration"), Montage->GetPlayLength());
	TArray<TSharedPtr<FJsonValue>> Notifies;
	for (const auto& Notify : Montage->Notifies)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Notify.GetNotifyEventName().ToString());
		Entry->SetNumberField(TEXT("time"), Notify.GetTriggerTime());
		Notifies.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Root->SetArrayField(TEXT("notifies"), Notifies);
	TArray<TSharedPtr<FJsonValue>> Sections;
	for (int32 Index = 0; Index < Montage->CompositeSections.Num(); ++Index)
	{
		float Start, End;
		Montage->GetSectionStartAndEndTime(Index, Start, End);
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Montage->CompositeSections[Index].SectionName.ToString());
		Entry->SetNumberField(TEXT("start"), Start);
		Entry->SetNumberField(TEXT("end"), End);
		Sections.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Root->SetArrayField(TEXT("sections"), Sections);
	FString Result;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Result));
	return Result;
}

void UStingerAssetLibrary::ReattachComponent(USceneComponent* Component, USceneComponent* Parent, FName SocketName)
{
	if (Component && Parent) Component->SetupAttachment(Parent, SocketName);
}

void UStingerAssetLibrary::BeginValidationPIE(int32 Players)
{
	ULevelEditorPlaySettings* Settings = NewObject<ULevelEditorPlaySettings>();
	// Isolate the requested PIE topology from the user's saved multiplayer settings.
	Settings->bLaunchSeparateServer = false;
	Settings->SetPlayNumberOfClients(FMath::Clamp(Players, 1, 2));
	Settings->SetRunUnderOneProcess(true);
	Settings->SetPlayNetMode(Players > 1 ? PIE_ListenServer : PIE_Standalone);
	FRequestPlaySessionParams Params;
	Params.EditorPlaySettings = Settings;
	Params.bAllowOnlineSubsystem = false;
	GEditor->RequestPlaySession(Params);
}

bool UStingerAssetLibrary::SetValidationViewportSize(APlayerController* Controller, int32 Width, int32 Height)
{
	if (!Controller || !Controller->GetLocalPlayer() || Width < 1 || Height < 1 || Width > 3840 || Height > 2160) return
		false;
	UGameViewportClient* Client = Controller->GetLocalPlayer()->ViewportClient;
	if (!Client || !Client->GetGameViewport()) return false;
	Client->GetGameViewport()->SetFixedViewportSize(Width, Height);
	return true;
}

TArray<UWorld*> UStingerAssetLibrary::GetValidationWorlds()
{
	TArray<UWorld*> Worlds;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (Context.WorldType == EWorldType::PIE && Context.World()) Worlds.Add(Context.World());
	return Worlds;
}

void UStingerAssetLibrary::IsolateValidationInput(APlayerController* Controller)
{
	if (!Controller || !Controller->GetLocalPlayer()) return;
	if (auto* Subsystem = Controller->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		Subsystem->ClearAllMappings();
	Controller->SetControlRotation(FRotator::ZeroRotator);
}

void UStingerAssetLibrary::InjectValidationInput(APlayerController* Controller, UInputAction* Action, FVector Value)
{
	if (!Controller || !Action || !Controller->GetLocalPlayer()) return;
	if (auto* Subsystem = Controller->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
	{
		FInputActionValue InputValue;
		switch (Action->ValueType)
		{
		case EInputActionValueType::Boolean: InputValue = FInputActionValue(Value.X > 0.5);
			break;
		case EInputActionValueType::Axis1D: InputValue = FInputActionValue(static_cast<float>(Value.X));
			break;
		case EInputActionValueType::Axis2D: InputValue = FInputActionValue(FVector2D(Value.X, Value.Y));
			break;
		default: InputValue = FInputActionValue(Value);
			break;
		}
		Subsystem->InjectInputForAction(Action, InputValue, {}, {});
	}
}

void UStingerAssetLibrary::ResetAnimation(UAnimSequence* Sequence)
{
	if (!Sequence) return;
	Sequence->GetController().ResetModel(false);
	// 工厂已经通知 Populated；重导入期间允许设置非整数的源采样帧率。
	UObject* Model = Sequence->GetDataModelInterface().GetObject();
	if (FBoolProperty* Populated = FindFProperty<FBoolProperty>(Model->GetClass(), TEXT("bPopulated")))
		Populated->SetPropertyValue_InContainer(Model, false);
}

void UStingerAssetLibrary::FinalizeAnimation(UAnimSequence* Sequence)
{
	if (Sequence)
	{
		Sequence->GetController().NotifyPopulated();
		Sequence->MarkPackageDirty();
	}
}

void UStingerAssetLibrary::AddMeshSocket(USkeletalMesh* Mesh, FName SocketName, FName BoneName, FTransform Offset)
{
	if (!Mesh || Mesh->GetRefSkeleton().FindBoneIndex(BoneName) == INDEX_NONE) return;
	USkeletalMeshSocket* Socket = Mesh->FindSocket(SocketName);
	if (!Socket)
	{
		Socket = NewObject<USkeletalMeshSocket>(Mesh);
		Socket->SocketName = SocketName;
		Mesh->GetMeshOnlySocketList().Add(Socket);
	}
	Socket->BoneName = BoneName;
	Socket->RelativeLocation = Offset.GetLocation();
	Socket->RelativeRotation = Offset.Rotator();
	Socket->RelativeScale = Offset.GetScale3D();
	Mesh->MarkPackageDirty();
}

namespace
{
	void RemapProperties(UStruct* Type, void* Data, const TMap<FName, FName>& Map)
	{
		for (TFieldIterator<FProperty> It(Type); It; ++It)
		{
			FProperty* Property = *It;
			void* Value = Property->ContainerPtrToValuePtr<void>(Data);
			if (FNameProperty* Name = CastField<FNameProperty>(Property))
			{
				if (const FName* NewName = Map.Find(Name->GetPropertyValue(Value)))
					Name->SetPropertyValue(
						Value, *NewName);
			}
			else if (FStructProperty* Struct = CastField<FStructProperty>(Property))
				RemapProperties(
					Struct->Struct, Value, Map);
			else if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
			{
				if (FStructProperty* Inner = CastField<FStructProperty>(Array->Inner))
				{
					FScriptArrayHelper Helper(Array, Value);
					for (int32 Index = 0; Index < Helper.Num(); ++Index)
						RemapProperties(
							Inner->Struct, Helper.GetRawPtr(Index), Map);
				}
			}
		}
	}
}

void UStingerAssetLibrary::RemapBlueprintBones(UAnimBlueprint* Blueprint, const TMap<FName, FName>& BoneMap)
{
	if (!Blueprint) return;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
		for (UEdGraphNode* Node : Graph->Nodes) RemapProperties(Node->GetClass(), Node, BoneMap);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
}

UAnimMontage* UStingerAssetLibrary::CreateVisualMontage(UAnimSequence* Sequence, const FString& PackageName)
{
	if (!Sequence) return nullptr;
	UPackage* Package = CreatePackage(*PackageName);
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
	UAnimMontage* Montage = FindObject<UAnimMontage>(Package, *AssetName);
	if (!Montage)
	{
		Montage = NewObject<UAnimMontage>(Package, *AssetName, RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Montage);
	}
	Montage->SetSkeleton(Sequence->GetSkeleton());
	Montage->SlotAnimTracks.Reset();
	FSlotAnimationTrack& Slot = Montage->SlotAnimTracks.AddDefaulted_GetRef();
	Slot.SlotName = TEXT("DefaultSlot");
	FAnimSegment& Segment = Slot.AnimTrack.AnimSegments.AddDefaulted_GetRef();
	Segment.SetAnimReference(Sequence);
	Segment.AnimEndTime = Sequence->GetPlayLength();
	Segment.AnimPlayRate = 1.f;
	Segment.LoopingCount = 1;
	Montage->SetCompositeLength(Sequence->GetPlayLength());
	Montage->CompositeSections.Reset();
	Montage->AddAnimCompositeSection(TEXT("Default"), 0.f);
	Montage->BlendIn.SetBlendTime(FMath::Min(.04f, Sequence->GetPlayLength() * .1f));
	Montage->BlendOut.SetBlendTime(FMath::Min(.04f, Sequence->GetPlayLength() * .1f));
	Montage->MarkPackageDirty();
	return Montage;
}
