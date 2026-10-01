#include "StingerAssetLibrary.h"
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
	// Read reflected state without loading gameplay modules into the import helper.
	auto ObjectField = [](UObject* Object, FName Name) -> UObject*
	{
		if (!Object) return nullptr;
		auto Property = FindFProperty<FObjectPropertyBase>(Object->GetClass(), Name);
		return Property ? Property->GetObjectPropertyValue_InContainer(Object) : nullptr;
	};
	UObject* Combat = ObjectField(Pawn, TEXT("Combat"));
	UObject* Weapon = ObjectField(Combat, TEXT("EquippedWeapon"));
	TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
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
		State->SetStringField(
			TEXT("body_hand_actor_space"),
			Body->GetSocketTransform(TEXT("RightHand")).GetRelativeTransform(Pawn->GetActorTransform()).ToString());
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
	Settings->SetPlayNumberOfClients(FMath::Clamp(Players, 1, 2));
	Settings->SetRunUnderOneProcess(true);
	Settings->SetPlayNetMode(Players > 1 ? PIE_ListenServer : PIE_Standalone);
	FRequestPlaySessionParams Params;
	Params.EditorPlaySettings = Settings;
	Params.bAllowOnlineSubsystem = false;
	GEditor->RequestPlaySession(Params);
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
