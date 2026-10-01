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
	}
	FString Result;
	FJsonSerializer::Serialize(State, TJsonWriterFactory<>::Create(&Result));
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
	Montage->MarkPackageDirty();
	return Montage;
}
