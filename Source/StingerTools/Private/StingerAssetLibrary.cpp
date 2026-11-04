#include "StingerAssetLibrary.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Misc/FileHelper.h"
#include "UObject/SavePackage.h"
#include "AnimGraphNode_RifleLocomotion.h"
#include "Animation/BlendSpace.h"
#include "Engine/GameViewportClient.h"
#include "Slate/SceneViewport.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "AnimationRuntime.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
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
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_TraversalContact.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "UObject/UObjectIterator.h"
#include "EngineUtils.h"
#include "BrokenEdge/Extraction/ExtractionWater.h"
#include "MeshDescription.h"
#include "AssetCompilingManager.h"

bool UStingerAssetLibrary::PopulateLocomotionBlendSpace(UBlendSpace* BlendSpace,
                                                        const TArray<UAnimSequence*>& Animations,
                                                        const TArray<FVector>& Coordinates, float MaxSpeed,
                                                        bool bAimOffset)
{
	if (!BlendSpace || Animations.IsEmpty() || Animations.Num() != Coordinates.Num()) return false;
	for (auto* Sequence : Animations)
		if (!Sequence || Sequence->GetSkeleton() != BlendSpace->GetSkeleton()) return false;
	BlendSpace->Modify();
	for (int32 Index = BlendSpace->GetNumberOfBlendSamples() - 1; Index >= 0; --Index) BlendSpace->DeleteSample(Index);
	auto& X = const_cast<FBlendParameter&>(BlendSpace->GetBlendParameter(0));
	X.DisplayName = bAimOffset ? TEXT("Pitch") : TEXT("Direction");
	X.Min = bAimOffset ? -80.f : -180.f;
	X.Max = bAimOffset ? 80.f : 180.f;
	X.GridNum = bAimOffset ? 4 : 8;
	auto& Y = const_cast<FBlendParameter&>(BlendSpace->GetBlendParameter(1));
	Y.DisplayName = TEXT("Speed");
	Y.Min = 0.f;
	Y.Max = MaxSpeed;
	Y.GridNum = 8;
	for (int32 Index = 0; Index < Animations.Num(); ++Index)
		if (BlendSpace->AddSample(Animations[Index], Coordinates[Index]) == INDEX_NONE) return false;
	BlendSpace->ValidateSampleData();
	BlendSpace->ResampleData();
	BlendSpace->PostEditChange();
	BlendSpace->MarkPackageDirty();
	return true;
}

bool UStingerAssetLibrary::ConfigureRifleLocomotion(UAnimBlueprint* Blueprint)
{
	if (!Blueprint || !Blueprint->GetPathName().StartsWith(TEXT("/Game/Characters/Stinger/"))) return false;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (auto* Graph : Graphs)
		for (auto Node : Graph->Nodes)
			if (Cast<UAnimGraphNode_RifleLocomotion>(Node)) return true;
	// Insert before the weapon montage, preserving subsequent aiming, grip IK and traversal layers.
	for (auto* Graph : Graphs)
	{
		for (auto Node : Graph->Nodes)
		{
			auto* Slot = Cast<UAnimGraphNode_Slot>(Node);
			if (!Slot || Slot->Node.SlotName != TEXT("WeaponSlot")) continue;
			auto* Input = Slot->FindPin(TEXT("Source"));
			if (!Input || Input->LinkedTo.Num() != 1) return false;
			auto* Previous = Input->LinkedTo[0];
			FGraphNodeCreator<UAnimGraphNode_RifleLocomotion> Creator(*Graph);
			auto* Movement = Creator.CreateNode();
			Creator.Finalize();
			Input->BreakLinkTo(Previous);
			Previous->MakeLinkTo(Movement->FindPinChecked(TEXT("Source")));
			Movement->FindPinChecked(TEXT("Pose"), EGPD_Output)->MakeLinkTo(Input);
			Movement->NodePosX = Slot->NodePosX - 240;
			Movement->NodePosY = Slot->NodePosY;
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			return Blueprint->Status != BS_Error;
		}
	}
	return false;
}

void UStingerAssetLibrary::FinishValidationAssetCompilation()
{
	FlushAsyncLoading();
	FAssetCompilingManager::Get().FinishAllCompilation();
}

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

FString UStingerAssetLibrary::DescribeSkeletalVertices(USkeletalMesh* Mesh)
{
	TArray<TSharedPtr<FJsonValue>> Points;
	if (Mesh && Mesh->GetResourceForRendering() && Mesh->GetResourceForRendering()->LODRenderData.Num())
	{
		const auto& Positions = Mesh->GetResourceForRendering()->LODRenderData[0].StaticVertexBuffers.
		                              PositionVertexBuffer;
		for (uint32 I = 0; I < Positions.GetNumVertices(); ++I)
		{
			const auto P = Positions.VertexPosition(I);
			Points.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
				MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y), MakeShared<FJsonValueNumber>(P.Z)
			}));
		}
	}
	FString Result;
	FJsonSerializer::Serialize(Points, TJsonWriterFactory<>::Create(&Result));
	return Result;
}

int32 UStingerAssetLibrary::RenderWeaponIcon(USkeletalMesh* Mesh, const FString& File, int32 Height, float Margin)
{
	if (!Mesh || !Mesh->GetResourceForRendering() || !Mesh->GetResourceForRendering()->LODRenderData.Num()) return 0;
	const auto& Lod = Mesh->GetResourceForRendering()->LODRenderData[0];
	const auto& Positions = Lod.StaticVertexBuffers.PositionVertexBuffer;
	const auto* Indices = Lod.MultiSizeIndexContainer.GetIndexBuffer();
	if (!Indices || Indices->Num() < 3 || Positions.GetNumVertices() == 0) return 0;
	FBox Bounds(ForceInit);
	for (uint32 I = 0; I < Positions.GetNumVertices(); ++I) Bounds += FVector(Positions.VertexPosition(I));
	// 长轴为前后方向；枪口一侧放在右边。
	const bool bAlongY = Bounds.GetExtent().Y > Bounds.GetExtent().X;
	float Sign = 1.f;
	if (const auto* Muzzle = Mesh->FindSocket(TEXT("MuzzleFlash")))
	{
		const FVector At = Muzzle->GetSocketLocalTransform().GetLocation()
			+ (Mesh->GetRefSkeleton().FindBoneIndex(Muzzle->BoneName) != INDEX_NONE
				   ? FAnimationRuntime::GetComponentSpaceTransformRefPose(
					   Mesh->GetRefSkeleton(), Mesh->GetRefSkeleton().FindBoneIndex(Muzzle->BoneName)).GetLocation()
				   : FVector::ZeroVector);
		Sign = (bAlongY ? At.Y - Bounds.GetCenter().Y : At.X - Bounds.GetCenter().X) < 0 ? -1.f : 1.f;
	}
	// 投影：U 沿枪身（枪口为正），V 向下，深度取侧向（离观察者近的覆盖远的）。
	const auto Project = [&](const FVector& P) { return FVector(bAlongY ? P.Y * Sign : P.X * Sign, -P.Z,
	                                                            bAlongY ? -P.X * Sign : P.Y * Sign); };
	FBox Flat(ForceInit);
	for (uint32 I = 0; I < Positions.GetNumVertices(); ++I) Flat += Project(FVector(Positions.VertexPosition(I)));
	const int32 Super = 4;
	const double Inner = 1. - 2. * Margin;
	const double Scale = Height * Inner / FMath::Max(1., Flat.GetSize().Y);
	const int32 Width = FMath::Clamp(FMath::CeilToInt(Flat.GetSize().X * Scale + Height * 2. * Margin), 8, 4096);
	const int32 W = Width * Super, H = Height * Super;
	TArray<float> Depth;
	Depth.Init(TNumericLimits<float>::Lowest(), W * H);
	TArray<float> Shade;
	Shade.Init(-1.f, W * H);
	const FVector Light = FVector(.35, -.75, .55).GetSafeNormal(); // 投影空间：右上偏前方打光
	const auto ToPixel = [&](const FVector& P)
	{
		return FVector((P.X - Flat.Min.X) * Scale * Super + Height * Margin * Super,
		               (P.Y - Flat.Min.Y) * Scale * Super + Height * Margin * Super, P.Z);
	};
	for (int32 T = 0; T + 2 < Indices->Num(); T += 3)
	{
		const FVector A = Project(FVector(Positions.VertexPosition(Indices->Get(T))));
		const FVector B = Project(FVector(Positions.VertexPosition(Indices->Get(T + 1))));
		const FVector C = Project(FVector(Positions.VertexPosition(Indices->Get(T + 2))));
		FVector N = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		if (N.Z < 0) N = -N; // 双面：法线朝观察者
		const float Lit = .45f + .55f * FMath::Max(0.f, static_cast<float>(FVector::DotProduct(N, Light)));
		const FVector PA = ToPixel(A), PB = ToPixel(B), PC = ToPixel(C);
		const double Area = (PB.X - PA.X) * (PC.Y - PA.Y) - (PB.Y - PA.Y) * (PC.X - PA.X);
		if (FMath::Abs(Area) < 1e-9) continue;
		const int32 X0 = FMath::Max(0, FMath::FloorToInt(FMath::Min3(PA.X, PB.X, PC.X)));
		const int32 X1 = FMath::Min(W - 1, FMath::CeilToInt(FMath::Max3(PA.X, PB.X, PC.X)));
		const int32 Y0 = FMath::Max(0, FMath::FloorToInt(FMath::Min3(PA.Y, PB.Y, PC.Y)));
		const int32 Y1 = FMath::Min(H - 1, FMath::CeilToInt(FMath::Max3(PA.Y, PB.Y, PC.Y)));
		for (int32 Y = Y0; Y <= Y1; ++Y)
			for (int32 X = X0; X <= X1; ++X)
			{
				const double Px = X + .5, Py = Y + .5;
				const double U = ((PB.X - Px) * (PC.Y - Py) - (PB.Y - Py) * (PC.X - Px)) / Area;
				const double V = ((PC.X - Px) * (PA.Y - Py) - (PC.Y - Py) * (PA.X - Px)) / Area;
				const double Wt = 1. - U - V;
				if (U < 0 || V < 0 || Wt < 0) continue;
				const float Z = U * PA.Z + V * PB.Z + Wt * PC.Z;
				const int32 Index = Y * W + X;
				if (Z <= Depth[Index]) continue;
				Depth[Index] = Z;
				Shade[Index] = Lit;
			}
	}
	TArray<FColor> Pixels;
	Pixels.SetNumZeroed(Width * Height);
	const FLinearColor Base = FLinearColor(FColor(0xC9, 0xCF, 0xD6));
	for (int32 Y = 0; Y < Height; ++Y)
		for (int32 X = 0; X < Width; ++X)
		{
			float Cover = 0, Lum = 0;
			for (int32 Sy = 0; Sy < Super; ++Sy)
				for (int32 Sx = 0; Sx < Super; ++Sx)
					if (const float S = Shade[(Y * Super + Sy) * W + X * Super + Sx]; S >= 0.f)
					{
						Cover += 1;
						Lum += S;
					}
			if (Cover <= 0) continue;
			FLinearColor C = Base * (Lum / Cover);
			C.A = Cover / (Super * Super);
			Pixels[Y * Width + X] = C.ToFColor(true);
		}
	auto& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Png = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!Png.IsValid() || !Png->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Width, Height,
	                                   ERGBFormat::BGRA, 8))
		return 0;
	return FFileHelper::SaveArrayToFile(Png->GetCompressed(), *File) ? Width : 0;
}

int32 UStingerAssetLibrary::RenderLevelOverview(UObject* WorldContext, FVector2D Min, FVector2D Max, int32 Size,
                                                const FString& File)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || Size < 16 || Max.X <= Min.X || Max.Y <= Min.Y) return 0;
	struct FTris
	{
		TArray<FVector3f> Positions;
		TArray<uint32> Indices;
	};
	TMap<const UStaticMesh*, FTris> Cache;
	const auto Load = [&](const UStaticMesh* Mesh) -> const FTris&
	{
		if (const FTris* Found = Cache.Find(Mesh)) return *Found;
		FTris& Out = Cache.Add(Mesh);
		// 优先用渲染数据里较粗的 LOD（俯视 15 cm 一像素足够），没有 CPU 副本时退回源网格描述。
		if (const auto* Render = Mesh->GetRenderData(); Render && Render->LODResources.Num())
		{
			const auto& Lod = Render->LODResources[FMath::Min(2, Render->LODResources.Num() - 1)];
			const auto& Buffer = Lod.VertexBuffers.PositionVertexBuffer;
			if (Buffer.GetVertexData() && Buffer.GetNumVertices() && Lod.IndexBuffer.GetNumIndices())
			{
				Lod.IndexBuffer.GetCopy(Out.Indices);
				if (Out.Indices.Num())
				{
					Out.Positions.SetNum(Buffer.GetNumVertices());
					for (uint32 I = 0; I < Buffer.GetNumVertices(); ++I) Out.Positions[I] = Buffer.VertexPosition(I);
					return Out;
				}
			}
		}
		if (const auto* Description = Mesh->GetMeshDescription(0))
		{
			const FVector3f Scale(Mesh->GetSourceModel(0).BuildSettings.BuildScale3D);
			const auto Positions = Description->GetVertexPositions();
			TMap<FVertexID, uint32> Remap;
			for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
				for (const FVertexID Vertex : Description->GetTriangleVertices(Triangle))
				{
					const uint32* Index = Remap.Find(Vertex);
					if (!Index)
					{
						Index = &Remap.Add(Vertex, Out.Positions.Num());
						Out.Positions.Add(Positions[Vertex] * Scale);
					}
					Out.Indices.Add(*Index);
				}
		}
		return Out;
	};
	const double Cell = FMath::Max(Max.X - Min.X, Max.Y - Min.Y) / Size;
	TArray<float> Height;
	Height.Init(TNumericLimits<float>::Lowest(), Size * Size);
	const FBox Area(FVector(Min.X, Min.Y, -1e6), FVector(Max.X, Max.Y, 1e6));
	int32 Drawn = 0;
	const auto Raster = [&](const FTris& Tris, const FTransform& ToWorld)
	{
		TArray<FVector> World;
		World.SetNum(Tris.Positions.Num());
		for (int32 I = 0; I < Tris.Positions.Num(); ++I)
		{
			const FVector P = ToWorld.TransformPosition(FVector(Tris.Positions[I]));
			// 像素空间：列 = Y，行 = 北在上。
			World[I] = FVector((P.Y - Min.Y) / Cell, (Max.X - P.X) / Cell, P.Z);
		}
		for (int32 T = 0; T + 2 < Tris.Indices.Num(); T += 3)
		{
			const FVector& A = World[Tris.Indices[T]];
			const FVector& B = World[Tris.Indices[T + 1]];
			const FVector& C = World[Tris.Indices[T + 2]];
			const double Det = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
			const int32 X0 = FMath::Max(0, FMath::FloorToInt(FMath::Min3(A.X, B.X, C.X)));
			const int32 X1 = FMath::Min(Size - 1, FMath::FloorToInt(FMath::Max3(A.X, B.X, C.X)));
			const int32 Y0 = FMath::Max(0, FMath::FloorToInt(FMath::Min3(A.Y, B.Y, C.Y)));
			const int32 Y1 = FMath::Min(Size - 1, FMath::FloorToInt(FMath::Max3(A.Y, B.Y, C.Y)));
			if (X0 > X1 || Y0 > Y1) continue;
			if (FMath::Abs(Det) < 1e-6)
			{
				// 竖直面在俯视里退化成线：墙体只有几厘米厚时靠它留下轮廓。
				const float Top = FMath::Max3(A.Z, B.Z, C.Z);
				if (X1 - X0 <= 2 && Y1 - Y0 <= 2)
					for (int32 Y = Y0; Y <= Y1; ++Y)
						for (int32 X = X0; X <= X1; ++X)
							Height[Y * Size + X] = FMath::Max(Height[Y * Size + X], Top);
				continue;
			}
			for (int32 Y = Y0; Y <= Y1; ++Y)
				for (int32 X = X0; X <= X1; ++X)
				{
					const double Px = X + .5, Py = Y + .5;
					double U = ((B.X - Px) * (C.Y - Py) - (B.Y - Py) * (C.X - Px)) / Det;
					double V = ((C.X - Px) * (A.Y - Py) - (C.Y - Py) * (A.X - Px)) / Det;
					double W = 1. - U - V;
					// 小于一个像素的三角形也要落点，放宽半个像素。
					const double Slack = .5 / FMath::Max(1., FMath::Abs(Det));
					if (U < -Slack || V < -Slack || W < -Slack) continue;
					U = FMath::Clamp(U, 0., 1.);
					V = FMath::Clamp(V, 0., 1.);
					W = FMath::Clamp(W, 0., 1.);
					const float Z = (U * A.Z + V * B.Z + W * C.Z) / FMath::Max(1e-6, U + V + W);
					float& H = Height[Y * Size + X];
					H = FMath::Max(H, Z);
				}
		}
	};
	for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
	{
		UStaticMeshComponent* Component = *It;
		if (Component->GetWorld() != World || !Component->IsRegistered() || !Component->GetStaticMesh()
			|| Component->bHiddenInGame || !Component->IsVisible()
			|| Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
			continue;
		const FBox Bounds = Component->Bounds.GetBox();
		if (!Bounds.Intersect(Area) || Bounds.GetSize().X > 4 * (Max.X - Min.X)) continue;
		const FTris& Tris = Load(Component->GetStaticMesh());
		if (Tris.Indices.IsEmpty()) continue;
		if (const auto* Instanced = Cast<UInstancedStaticMeshComponent>(Component))
		{
			for (int32 I = 0; I < Instanced->GetInstanceCount(); ++I)
			{
				FTransform Instance;
				if (Instanced->GetInstanceTransform(I, Instance, true)) Raster(Tris, Instance);
			}
		}
		else Raster(Tris, Component->GetComponentTransform());
		++Drawn;
	}
	// 文件名以 .r32 结尾时只输出原始高度（float32，行优先，北在上；无几何处为 -3.4e38），供脚本分析地形。
	if (File.EndsWith(TEXT(".r32")))
	{
		TArray<uint8> Raw;
		Raw.Append(reinterpret_cast<const uint8*>(Height.GetData()), Height.Num() * sizeof(float));
		return FFileHelper::SaveArrayToFile(Raw, *File) ? Drawn : 0;
	}
	// 高度分档上色 + 西北向山体阴影 + 高差处描边（墙、楼边、台阶）。
	TArray<float> Known;
	for (int32 I = 0; I < Height.Num(); I += 7)
		if (Height[I] > -1e30f) Known.Add(Height[I]);
	if (Known.IsEmpty()) return 0;
	Known.Sort();
	const float Low = Known[Known.Num() * 5 / 100], High = FMath::Max(Low + 100.f, Known[Known.Num() * 97 / 100]);
	const FLinearColor Empty(.035f, .05f, .058f), Ground(.10f, .14f, .145f), Top(.50f, .56f, .54f), Edge(.80f, .86f, .82f);
	const FVector Sun = FVector(1, -1, 1.4).GetSafeNormal(); // 北 = 行减小，西 = 列减小
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Size * Size);
	const auto At = [&](int32 X, int32 Y)
	{
		X = FMath::Clamp(X, 0, Size - 1);
		Y = FMath::Clamp(Y, 0, Size - 1);
		return Height[Y * Size + X];
	};
	// 局部地面：约 22 m 半径内的最低点（可分离滑动最小值），再做一次盒式平滑。
	// 颜色主要按“比周围地面高多少”分档，山地地形本身再高也是地面色，建筑和平台才会亮。
	TArray<float> Floor = Height;
	for (float& H : Floor)
		if (H <= -1e30f) H = TNumericLimits<float>::Max();
	const int32 Reach = FMath::Max(4, FMath::RoundToInt(2200. / Cell));
	const auto SlideMin = [&](TArray<float>& Data, bool bRows)
	{
		TArray<float> Line, Out;
		Line.SetNumUninitialized(Size);
		Out.SetNumUninitialized(Size);
		for (int32 A = 0; A < Size; ++A)
		{
			for (int32 B = 0; B < Size; ++B) Line[B] = Data[bRows ? A * Size + B : B * Size + A];
			TArray<int32> Queue;
			int32 Head = 0;
			for (int32 B = 0; B < Size + Reach; ++B)
			{
				if (B < Size)
				{
					while (Queue.Num() > Head && Line[Queue.Last()] >= Line[B]) Queue.Pop(EAllowShrinking::No);
					Queue.Add(B);
				}
				const int32 Center = B - Reach;
				if (Center < 0) continue;
				while (Queue[Head] < Center - Reach) ++Head;
				Out[Center] = Line[Queue[Head]];
			}
			for (int32 B = 0; B < Size; ++B) Data[bRows ? A * Size + B : B * Size + A] = Out[B];
		}
	};
	SlideMin(Floor, true);
	SlideMin(Floor, false);
	{
		const int32 Blur = Reach / 2;
		TArray<double> Sum;
		Sum.SetNumZeroed((Size + 1) * (Size + 1));
		for (int32 Y = 0; Y < Size; ++Y)
			for (int32 X = 0; X < Size; ++X)
			{
				const float F = Floor[Y * Size + X];
				Sum[(Y + 1) * (Size + 1) + X + 1] = (F > 1e30f ? 0. : F) + Sum[Y * (Size + 1) + X + 1]
					+ Sum[(Y + 1) * (Size + 1) + X] - Sum[Y * (Size + 1) + X];
			}
		for (int32 Y = 0; Y < Size; ++Y)
			for (int32 X = 0; X < Size; ++X)
			{
				const int32 X0 = FMath::Max(0, X - Blur), X1 = FMath::Min(Size, X + Blur + 1);
				const int32 Y0 = FMath::Max(0, Y - Blur), Y1 = FMath::Min(Size, Y + Blur + 1);
				const double Total = Sum[Y1 * (Size + 1) + X1] - Sum[Y0 * (Size + 1) + X1] - Sum[Y1 * (Size + 1) + X0]
					+ Sum[Y0 * (Size + 1) + X0];
				if (Floor[Y * Size + X] < 1e30f)
					Floor[Y * Size + X] = FMath::Min(Height[Y * Size + X],
					                                 static_cast<float>(Total / ((X1 - X0) * (Y1 - Y0))));
			}
	}
	for (int32 Y = 0; Y < Size; ++Y)
		for (int32 X = 0; X < Size; ++X)
		{
			const float H = At(X, Y);
			if (H <= -1e30f)
			{
				Pixels[Y * Size + X] = Empty.ToFColor(true);
				continue;
			}
			float Rise = 0;
			for (const FIntPoint D : {FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)})
			{
				const float N = At(X + D.X, Y + D.Y);
				Rise = FMath::Max(Rise, N <= -1e30f ? 300.f : FMath::Abs(N - H));
			}
			const float L = At(X - 1, Y) <= -1e30f ? H : At(X - 1, Y), R = At(X + 1, Y) <= -1e30f ? H : At(X + 1, Y);
			const float U = At(X, Y - 1) <= -1e30f ? H : At(X, Y - 1), Dn = At(X, Y + 1) <= -1e30f ? H : At(X, Y + 1);
			// 坡度按 3 倍夸张，平地上的台阶和路缘也看得出来。
			const FVector Normal = FVector(-(R - L) / (2 * Cell) * 3, (Dn - U) / (2 * Cell) * 3, 1).GetSafeNormal();
			const float Shade = .62f + .38f * FMath::Max(0.f, static_cast<float>(FVector::DotProduct(Normal, Sun)));
			const float Absolute = FMath::Clamp((H - Low) / (High - Low), 0.f, 1.f);
			const float Relative = FMath::Clamp((H - Floor[Y * Size + X] - 350.f) / 900.f, 0.f, 1.f);
			FLinearColor C = FMath::Lerp(Ground, Top, FMath::Pow(Relative, .7f)) * Shade * (.85f + .3f * Absolute);
			C = FMath::Lerp(C, Edge, FMath::Clamp((Rise - 90.f) / 260.f, 0.f, .8f));
			C.A = 1;
			Pixels[Y * Size + X] = C.ToFColor(true);
		}
	// 开局就有水的水体：水面以下的地面涂成水色，越深越暗。
	for (TActorIterator<AExtractionWater> It(World); It; ++It)
	{
		if (!It->bStartsFull && It->StartZ <= It->BottomZ) continue;
		const FVector O = It->GetActorLocation();
		const float Surface = It->SurfaceWorldZ();
		const int32 X0 = FMath::Max(0, FMath::FloorToInt((O.Y - It->HalfSize.Y - Min.Y) / Cell));
		const int32 X1 = FMath::Min(Size - 1, FMath::CeilToInt((O.Y + It->HalfSize.Y - Min.Y) / Cell));
		const int32 Y0 = FMath::Max(0, FMath::FloorToInt((Max.X - (O.X + It->HalfSize.X)) / Cell));
		const int32 Y1 = FMath::Min(Size - 1, FMath::CeilToInt((Max.X - (O.X - It->HalfSize.X)) / Cell));
		for (int32 Y = Y0; Y <= Y1; ++Y)
			for (int32 X = X0; X <= X1; ++X)
			{
				const float H = Height[Y * Size + X];
				if (H <= -1e30f || H >= Surface) continue;
				const float Depth = FMath::Clamp((Surface - H) / 1500.f, 0.f, 1.f);
				FLinearColor C = FMath::Lerp(FLinearColor(.10f, .30f, .40f), FLinearColor(.035f, .12f, .19f), Depth);
				C.A = 1;
				Pixels[Y * Size + X] = C.ToFColor(true);
			}
	}
	auto& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Png = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!Png.IsValid() || !Png->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Size, Size, ERGBFormat::BGRA, 8))
		return 0;
	UE_LOG(LogTemp, Display, TEXT("LEVEL_OVERVIEW components=%d meshes=%d low=%.0f high=%.0f"), Drawn, Cache.Num(), Low,
	       High);
	return FFileHelper::SaveArrayToFile(Png->GetCompressed(), *File) ? Drawn : 0;
}

FString UStingerAssetLibrary::DescribeLadderVertices(UStaticMesh* Mesh)
{
	TArray<TSharedPtr<FJsonValue>> Points;
	if (Mesh)
		if (const auto* Description = Mesh->GetMeshDescription(0))
		{
			const auto Positions = Description->GetVertexPositions();
			for (const auto Vertex : Description->Vertices().GetElementIDs())
			{
				const FVector3f P = Positions[Vertex] * FVector3f(Mesh->GetSourceModel(0).BuildSettings.BuildScale3D);
				Points.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
					MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y),
					MakeShared<FJsonValueNumber>(P.Z)
				}));
			}
			FString Result;
			FJsonSerializer::Serialize(Points, TJsonWriterFactory<>::Create(&Result));
			return Result;
		}
	if (Mesh && Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num())
	{
		const auto& Positions = Mesh->GetRenderData()->LODResources[0].VertexBuffers.PositionVertexBuffer;
		for (uint32 I = 0; I < Positions.GetNumVertices(); ++I)
		{
			const auto P = Positions.VertexPosition(I);
			Points.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
				MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y), MakeShared<FJsonValueNumber>(P.Z)
			}));
		}
	}
	FString Result;
	FJsonSerializer::Serialize(Points, TJsonWriterFactory<>::Create(&Result));
	return Result;
}

TArray<FVector> UStingerAssetLibrary::TraceTraversalSurface(UObject* WorldContext, FVector Start, FVector End)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	FHitResult Hit;
	if (World && World->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn,
	                                             FCollisionQueryParams(
		                                             SCENE_QUERY_STAT(TraversalEditorSurface), false)))
		return {Hit.ImpactPoint, Hit.ImpactNormal};
	return {};
}

bool UStingerAssetLibrary::TraversalCapsuleClear(UObject* WorldContext, FVector Start, FVector End, float Radius,
                                                 float HalfHeight)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	FHitResult Hit;
	return World && !World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Pawn,
	                                             FCollisionShape::MakeCapsule(Radius, HalfHeight),
	                                             FCollisionQueryParams(
		                                             SCENE_QUERY_STAT(TraversalEditorCapsule), false));
}

bool UStingerAssetLibrary::ConfigureTraversalSlot(UAnimBlueprint* Blueprint)
{
	if (!Blueprint || !Blueprint->GetPathName().StartsWith(TEXT("/Game/Characters/Stinger/"))) return false;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (auto* Graph : Graphs)
	{
		if (Graph->GetFName() != TEXT("AnimGraph")) continue;
		UAnimGraphNode_Root* Root = nullptr;
		for (auto Node : Graph->Nodes)
			if (auto* Candidate = Cast<UAnimGraphNode_Root>(Node)) Root = Candidate;
		if (!Root) return false;
		auto* Result = Root->FindPinChecked(TEXT("Result"));
		if (Result->LinkedTo.Num() != 1) return false;
		auto* Previous = Result->LinkedTo[0];
		if (Cast<UAnimGraphNode_TraversalContact>(Previous->GetOwningNode())) return true;
		FGraphNodeCreator<UAnimGraphNode_Slot> Creator(*Graph);
		auto* Slot = Creator.CreateNode();
		Slot->Node.SlotName = TEXT("DefaultSlot");
		Slot->NodePosX = Root->NodePosX;
		Slot->NodePosY = Root->NodePosY;
		Creator.Finalize();
		Result->BreakLinkTo(Previous);
		Previous->MakeLinkTo(Slot->FindPinChecked(TEXT("Source")));
		FGraphNodeCreator<UAnimGraphNode_TraversalContact> ContactCreator(*Graph);
		auto* Contact = ContactCreator.CreateNode();
		ContactCreator.Finalize();
		Slot->FindPinChecked(TEXT("Pose"), EGPD_Output)->MakeLinkTo(Contact->FindPinChecked(TEXT("Source")));
		Contact->FindPinChecked(TEXT("Pose"), EGPD_Output)->MakeLinkTo(Result);
		Contact->NodePosX = Root->NodePosX + 300;
		Contact->NodePosY = Root->NodePosY;
		Root->NodePosX += 600;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return Blueprint->Status != BS_Error;
	}
	return false;
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
	if (!Controller || !Controller->GetLocalPlayer() || Width < 1 || Height < 1 || Width > 3840 || Height > 2160)
		return
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

// Dump the anim graph so the slot order can be audited without opening the editor UI.
FString UStingerAssetLibrary::DescribeAnimGraphChain(UAnimBlueprint* Blueprint)
{
	if (!Blueprint) return TEXT("NO_BLUEPRINT");
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	FString Out;
	for (auto* Graph : Graphs)
	{
		Out += FString::Printf(TEXT("GRAPH %s\n"), *Graph->GetName());
		for (auto Node : Graph->Nodes)
		{
			if (!Node) continue;
			FString Extra;
			if (auto* Slot = Cast<UAnimGraphNode_Slot>(Node)) Extra = FString::Printf(TEXT(" SlotName=%s"), *Slot->Node.SlotName.ToString());
			if (auto* Layer = Cast<UAnimGraphNode_LayeredBoneBlend>(Node))
			{
				Extra = FString::Printf(TEXT(" Layers=%d Weights=%d BlendMode=%d"), Layer->Node.LayerSetup.Num(),
				                        Layer->Node.BlendWeights.Num(), static_cast<int32>(Layer->Node.BlendMode));
				for (int32 Index = 0; Index < Layer->Node.LayerSetup.Num(); ++Index)
				{
					Extra += TEXT(" [");
					for (const auto& Filter : Layer->Node.LayerSetup[Index].BranchFilters)
						Extra += FString::Printf(TEXT("%s:%d "), *Filter.BoneName.ToString(), Filter.BlendDepth);
					Extra += TEXT("]");
				}
			}
			Out += FString::Printf(TEXT("  NODE %s name=%s pos=(%d,%d)%s\n"), *Node->GetClass()->GetName(),
			                       *Node->GetName(), Node->NodePosX, Node->NodePosY, *Extra);
			for (auto* Pin : Node->Pins)
			{
				if (!Pin || Pin->LinkedTo.Num() == 0) continue;
				FString Links;
				for (auto* Link : Pin->LinkedTo)
					if (Link && Link->GetOwningNode())
						Links += FString::Printf(TEXT("%s.%s <- %s.%s | "), *Node->GetName(), *Pin->PinName.ToString(),
						                         *Link->GetOwningNode()->GetName(), *Link->PinName.ToString());
				Out += FString::Printf(TEXT("    PIN %s -> %s\n"), *Pin->PinName.ToString(), *Links);
			}
		}
	}
	return Out;
}

FString UStingerAssetLibrary::DescribeSkeletonBones(USkeleton* Skeleton)
{
	if (!Skeleton) return TEXT("NO_SKELETON");
	const auto& Bones = Skeleton->GetReferenceSkeleton();
	FString Out;
	for (int32 Index = 0; Index < Bones.GetNum(); ++Index)
		Out += FString::Printf(TEXT("%d %s parent=%d\n"), Index, *Bones.GetBoneName(Index).ToString(),
		                       Bones.GetParentIndex(Index));
	return Out;
}

bool UStingerAssetLibrary::SetMontageSlotName(UAnimMontage* Montage, FName SlotName)
{
	if (!Montage || SlotName.IsNone()) return false;
	bool bChanged = false;
	for (auto& Track : Montage->SlotAnimTracks)
	{
		if (Track.SlotName == SlotName) continue;
		Track.SlotName = SlotName;
		bChanged = true;
	}
	if (bChanged) Montage->MarkPackageDirty();
	return true;
}

// Insert <Slot> + layered blend per bone between the current tail node and the contact node,
// so melee montages replace the upper body only and the legs keep following locomotion.
bool UStingerAssetLibrary::ConfigureMeleeUpperBodySlot(UAnimBlueprint* Blueprint, FName SlotName, FName BranchBone)
{
	if (!Blueprint || SlotName.IsNone() || BranchBone.IsNone()) return false;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (auto* Graph : Graphs)
	{
		if (Graph->GetFName() != TEXT("AnimGraph")) continue;
		UAnimGraphNode_Root* Root = nullptr;
		for (auto Node : Graph->Nodes)
			if (auto* Candidate = Cast<UAnimGraphNode_Root>(Node)) Root = Candidate;
		if (!Root) return false;
		// Re-running must not stack a second layer.
		for (auto Node : Graph->Nodes)
			if (auto* Existing = Cast<UAnimGraphNode_Slot>(Node))
				if (Existing->Node.SlotName == SlotName) return true;
		auto* Result = Root->FindPinChecked(TEXT("Result"));
		if (Result->LinkedTo.Num() != 1) return false;
		// Result is fed by the contact node; walk one pin back to the pose that feeds it.
		auto* ContactOutput = Result->LinkedTo[0];
		if (!ContactOutput) return false;
		auto* ContactNode = ContactOutput->GetOwningNode();
		if (!ContactNode) return false;
		// The contact node exposes a single pose input; fall back to scanning for it.
		UEdGraphPin* ContactInput = ContactNode->FindPin(TEXT("Source"), EGPD_Input);
		if (!ContactInput || ContactInput->LinkedTo.Num() != 1)
		{
			ContactInput = nullptr;
			for (auto* Pin : ContactNode->Pins)
				if (Pin && Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() == 1) { ContactInput = Pin; break; }
		}
		if (!ContactInput) return false;
		auto* TailOutput = ContactInput->LinkedTo[0];
		if (!TailOutput) return false;
		auto* TailNode = TailOutput->GetOwningNode();
		if (!TailNode) return false;

		FGraphNodeCreator<UAnimGraphNode_Slot> SlotCreator(*Graph);
		auto* NewSlot = SlotCreator.CreateNode();
		NewSlot->Node.SlotName = SlotName;
		NewSlot->NodePosX = TailNode->NodePosX + 240;
		NewSlot->NodePosY = TailNode->NodePosY;
		SlotCreator.Finalize();

		FGraphNodeCreator<UAnimGraphNode_LayeredBoneBlend> BlendCreator(*Graph);
		auto* Blend = BlendCreator.CreateNode();
		BlendCreator.Finalize();
		Blend->NodePosX = TailNode->NodePosX + 480;
		Blend->NodePosY = TailNode->NodePosY;
		Blend->Node.BlendMode = ELayeredBoneBlendMode::BranchFilter;
		// The node constructor already adds the first pose; a node without one means the pin is missing.
		if (Blend->Node.LayerSetup.Num() == 0 || Blend->Node.BlendPoses.Num() == 0) return false;
		FBranchFilter Filter;
		Filter.BoneName = BranchBone;
		// Depth 1 gives the whole branch full weight, matching the existing reload upper body layer.
		Filter.BlendDepth = 1;
		Blend->Node.LayerSetup[0].BranchFilters.Reset();
		Blend->Node.LayerSetup[0].BranchFilters.Add(Filter);

		TailOutput->BreakLinkTo(ContactInput);
		TailOutput->MakeLinkTo(NewSlot->FindPinChecked(TEXT("Source")));
		TailOutput->MakeLinkTo(Blend->FindPinChecked(TEXT("BasePose")));
		NewSlot->FindPinChecked(TEXT("Pose"))->MakeLinkTo(Blend->FindPinChecked(TEXT("BlendPoses_0")));
		Blend->FindPinChecked(TEXT("Pose"))->MakeLinkTo(ContactInput);
		if (ContactInput->LinkedTo.Num() != 1 || ContactInput->LinkedTo[0]->GetOwningNode() != Blend) return false;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return Blueprint->Status != BS_Error;
	}
	return false;
}

UFont* UStingerAssetLibrary::CreateFontFromFiles(const TArray<FString>& Files, const TArray<FName>& Typefaces,
                                                 const FString& PackagePath, const FString& AssetName)
{
	if (Files.IsEmpty() || Files.Num() != Typefaces.Num()) return nullptr;
	auto Save = [](UObject* Asset)
	{
		FAssetRegistryModule::AssetCreated(Asset);
		Asset->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		const FString File = FPackageName::LongPackageNameToFilename(Asset->GetPackage()->GetName(),
		                                                             FPackageName::GetAssetPackageExtension());
		return UPackage::SavePackage(Asset->GetPackage(), Asset, *File, Args);
	};
	const FString FontName = PackagePath / AssetName;
	UFont* Font = NewObject<UFont>(CreatePackage(*FontName), *AssetName, RF_Public | RF_Standalone);
	Font->FontCacheType = EFontCacheType::Runtime;
	for (int32 Index = 0; Index < Files.Num(); ++Index)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Files[Index])) return nullptr;
		const FString FaceName = AssetName + TEXT("_") + Typefaces[Index].ToString();
		UFontFace* Face = NewObject<UFontFace>(CreatePackage(*(PackagePath / FaceName)), *FaceName,
		                                       RF_Public | RF_Standalone);
		Face->LoadingPolicy = EFontLoadingPolicy::Inline;
		Face->InitializeFromBulkData(Files[Index], EFontHinting::Default, Bytes.GetData(), Bytes.Num());
		if (!Save(Face)) return nullptr;
		FTypefaceEntry& Entry = Font->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.Emplace_GetRef(
			Typefaces[Index]);
		Entry.Font = FFontData(Face);
	}
	return Save(Font) ? Font : nullptr;
}
