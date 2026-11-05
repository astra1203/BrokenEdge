#include "SovietMilitaryBuilder.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraph/EdGraph.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "AssetCompilingManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "K2Node_AddComponent.h"
#include "StaticMeshResources.h"
#include "StaticMeshAttributes.h"
#include "MeshDescription.h"

namespace
{
	// Imported hard edges duplicate vertices; weld by position only for connectivity.
	// Separate panes keep independent damage even when the source batches a whole wall.
	TArray<TArray<FPolygonID>> GlassIslands(FMeshDescription& D)
	{
		FStaticMeshAttributes A(D);
		auto Positions = A.GetVertexPositions();
		TMap<FIntVector, TArray<FPolygonID>> AtPosition;
		TMap<FPolygonID, TArray<FIntVector>> Keys;
		for (auto P : D.Polygons().GetElementIDs())
			for (auto V : D.GetPolygonVertexInstances(P))
			{
				const FVector3f Point = Positions[D.GetVertexInstanceVertex(V)] * 100.f;
				const FIntVector Key(FMath::RoundToInt(Point.X), FMath::RoundToInt(Point.Y),
				                     FMath::RoundToInt(Point.Z));
				AtPosition.FindOrAdd(Key).AddUnique(P);
				Keys.FindOrAdd(P).AddUnique(Key);
			}
		TSet<FPolygonID> Seen;
		TArray<TArray<FPolygonID>> Islands;
		for (auto P : D.Polygons().GetElementIDs())
		{
			if (Seen.Contains(P)) continue;
			auto& Island = Islands.AddDefaulted_GetRef();
			Island.Add(P);
			Seen.Add(P);
			for (int32 Index = 0; Index < Island.Num(); ++Index)
				for (const auto& Key : Keys[Island[Index]])
					for (auto Neighbour : AtPosition[Key])
						if (!Seen.Contains(Neighbour))
						{
							Seen.Add(Neighbour);
							Island.Add(Neighbour);
						}
		}
		auto BoundsOf = [&](const TArray<FPolygonID>& Island)
		{
			FBox B(ForceInit);
			for (auto P : Island)
				for (auto V : D.GetPolygonVertexInstances(P))
					B += FVector(Positions[D.GetVertexInstanceVertex(V)]);
			return B;
		};
		for (int32 I = 0; I < Islands.Num(); ++I)
			for (int32 J = I + 1; J < Islands.Num(); ++J)
				if (BoundsOf(Islands[I]).ExpandBy(1.f).Intersect(BoundsOf(Islands[J])))
				{
					Islands[I].Append(Islands[J]);
					Islands.RemoveAt(J);
					J = I;
				}
		return Islands;
	}
}

int32 USovietMilitaryBuilder::MeshIslandCount(UStaticMesh* Mesh)
{
	FAssetCompilingManager::Get().FinishAllCompilation();
	if (!Mesh || !Mesh->GetMeshDescription(0)) return 0;
	return GlassIslands(*Mesh->GetMeshDescription(0)).Num();
}

bool USovietMilitaryBuilder::RetainMeshIsland(UStaticMesh* Mesh, int32 Island)
{
	if (!Mesh || !Mesh->GetPathName().StartsWith(TEXT("/Game/Extraction/Glass/"))) return false;
	FAssetCompilingManager::Get().FinishAllCompilation();
	auto* D = Mesh->GetMeshDescription(0);
	if (!D) return false;
	auto Islands = GlassIslands(*D);
	if (!Islands.IsValidIndex(Island)) return false;
	TArray<FPolygonID> Remove;
	for (int32 I = 0; I < Islands.Num(); ++I) if (I != Island) Remove.Append(Islands[I]);
	Mesh->Modify();
	D->DeletePolygons(Remove);
	FElementIDRemappings Remap;
	D->Compact(Remap);
	Mesh->CommitMeshDescription(0);
	TArray<int32> Materials;
	for (int32 I = 0; I < Mesh->GetStaticMaterials().Num(); ++I) Materials.Add(I);
	return RetainMeshSections(Mesh, Materials);
}

bool USovietMilitaryBuilder::RetainMeshSections(UStaticMesh* Mesh, const TArray<int32>& MaterialIndices)
{
	if (!Mesh || !Mesh->GetPathName().StartsWith(TEXT("/Game/Extraction/Glass/"))) return false;
	FAssetCompilingManager::Get().FinishAllCompilation();
	auto* Description = Mesh->GetMeshDescription(0);
	if (!Description) return false;
	Mesh->Modify();
	FStaticMeshAttributes Attributes(*Description);
	const auto Names = Attributes.GetPolygonGroupMaterialSlotNames();
	TArray<FPolygonID> Remove;
	for (auto Polygon : Description->Polygons().GetElementIDs())
	{
		const auto Group = Description->GetPolygonPolygonGroup(Polygon);
		const FName Name = Names[Group];
		int32 Material = Mesh->GetStaticMaterials().IndexOfByPredicate([&](const FStaticMaterial& M)
		{
			return M.ImportedMaterialSlotName == Name || M.MaterialSlotName == Name;
		});
		if (Material == INDEX_NONE) Material = Group.GetValue();
		if (!MaterialIndices.Contains(Material)) Remove.Add(Polygon);
	}
	if (Remove.Num() == Description->Polygons().Num()) return false;
	Description->DeletePolygons(Remove);
	FElementIDRemappings Remap;
	Description->Compact(Remap);
	Mesh->CommitMeshDescription(0);
	Mesh->SetNumSourceModels(1);
	// Transparent panes use raster rendering; opaque frames retain their source Nanite policy.
	if (!Mesh->GetName().StartsWith(TEXT("SM_Frame_"))) Mesh->GetNaniteSettings().bEnabled = false;
	Mesh->Build(false);
	FAssetCompilingManager::Get().FinishAllCompilation();
	if (!Mesh->GetBodySetup()) Mesh->CreateBodySetup();
	auto* Body = Mesh->GetBodySetup();
	Body->AggGeom.EmptyElements();
	Body->CollisionTraceFlag = CTF_UseComplexAsSimple;
	Body->bDoubleSidedGeometry = true;
	Mesh->MarkPackageDirty();
	return RefreshCollision(Mesh);
}

bool USovietMilitaryBuilder::ConfigureDoorCollision(UStaticMesh* Mesh)
{
	if (!Mesh || Mesh->GetName() != TEXT("SM_Door") || !Mesh->GetBodySetup()) return false;
	FAssetCompilingManager::Get().FinishAllCompilation();
	const FBox Bounds = Mesh->GetBoundingBox();
	if (!Bounds.IsValid || Bounds.GetSize().GetMin() <= 0) return false;
	Mesh->Modify();
	auto* Body = Mesh->GetBodySetup();
	Body->Modify();
	Body->AggGeom.EmptyElements();
	FKBoxElem& Box = Body->AggGeom.BoxElems.AddDefaulted_GetRef();
	Box.Center = Bounds.GetCenter();
	Box.X = FMath::Max(2., Bounds.GetSize().X);
	Box.Y = FMath::Max(2., Bounds.GetSize().Y);
	Box.Z = FMath::Max(2., Bounds.GetSize().Z);
	Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
	return RefreshCollision(Mesh) && Body->AggGeom.BoxElems.Num() == 1;
}

bool USovietMilitaryBuilder::ConfigureStairCollision(UStaticMesh* Mesh)
{
	if (!Mesh || (Mesh->GetName() != TEXT("SM_SmallStairSmall") && Mesh->GetName() != TEXT("SM_SmallStairBig")))
		return
			false;
	FAssetCompilingManager::Get().FinishAllCompilation();
	const auto* Data = Mesh->GetRenderData();
	if (!Data || Data->LODResources.IsEmpty() || !Mesh->GetBodySetup()) return false;
	const auto& Positions = Data->LODResources[0].VertexBuffers.PositionVertexBuffer;
	if (Positions.GetNumVertices() < 4) return false;
	Mesh->Modify();
	auto* Body = Mesh->GetBodySetup();
	Body->Modify();
	Body->AggGeom.EmptyElements();
	FKConvexElem& Hull = Body->AggGeom.ConvexElems.AddDefaulted_GetRef();
	for (uint32 Index = 0; Index < Positions.GetNumVertices(); ++Index)
		Hull.VertexData.Add(FVector(Positions.VertexPosition(Index)));
	Hull.UpdateElemBox();
	// Capsule sweeps use the convex envelope, while precise traces retain the visible steps.
	Body->CollisionTraceFlag = CTF_UseSimpleAndComplex;
	return RefreshCollision(Mesh);
}

int32 USovietMilitaryBuilder::ConfigureBackdrop(float MinimumDistance)
{
	auto* BP = LoadObject<UBlueprint>(
		nullptr, TEXT("/Game/UVB80/Assets/UVB80/Environment/TreesBackdrop/BP_TreesBackdropActor"));
	if (!BP || !FMath::IsFinite(MinimumDistance) || MinimumDistance < 0) return 0;
	TArray<UEdGraph*> Graphs;
	BP->GetAllGraphs(Graphs);
	int32 Count = 0;
	for (auto* Graph : Graphs)
		if (Graph)
			for (const auto& Node : Graph->Nodes)
				if (const auto* Add = Cast<UK2Node_AddComponent>(Node))
					if (auto* Template = Cast<UStaticMeshComponent>(Add->GetTemplateFromNode()))
					{
						Template->Modify();
						Template->MinDrawDistance = MinimumDistance;
						++Count;
					}
	if (!Count) return 0;
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	FKismetEditorUtilities::CompileBlueprint(BP);
	if (BP->Status == BS_Error || !UEditorLoadingAndSavingUtils::SavePackages({BP->GetOutermost()}, false)) return 0;
	return Count;
}

bool USovietMilitaryBuilder::RefreshCollision(UStaticMesh* Mesh)
{
	if (!Mesh || !Mesh->GetBodySetup()) return false;
	Mesh->Modify();
	Mesh->GetBodySetup()->InvalidatePhysicsData();
	Mesh->GetBodySetup()->CreatePhysicsMeshes();
	// Changing CollisionTraceFlag alone leaves the imported NavCollision geometry cached.
	// Force it to gather the same geometry used by the freshly rebuilt Chaos body.
	Mesh->CreateNavCollision(true);
	Mesh->MarkPackageDirty();
	return true;
}

FString USovietMilitaryBuilder::RepairWindows(bool Apply)
{
	FString Report;
	for (const TCHAR* Name : {TEXT("BP_WindowBase"), TEXT("BP_WindowA"), TEXT("BP_WindowB"), TEXT("BP_WindowC")})
	{
		const FString Path = FString(TEXT("/Game/UVB80/Assets/UVB80/Buildings/Blueprints/")) + Name;
		auto* BP = LoadObject<UBlueprint>(nullptr, *Path);
		if (!BP)
		{
			Report += TEXT("MISSING ") + Path;
			continue;
		}
		Report += TEXT("\nBLUEPRINT ") + Path;
		for (const auto& Var : BP->NewVariables)
			Report += TEXT("\nVAR ") + Var.VarName.ToString() + TEXT(" ") + Var.
			                                                                VarType.PinCategory.ToString() + TEXT(" ") +
				GetNameSafe(Var.VarType.PinSubCategoryObject.Get());
		if (BP->SimpleConstructionScript)
			for (auto* Node : BP->SimpleConstructionScript->GetAllNodes())
			{
				auto* Scene = Cast<USceneComponent>(Node->ComponentTemplate);
				Report += TEXT("\nNODE ") + Node->GetVariableName().ToString() + TEXT(" ") + GetNameSafe(
						Node->ComponentClass)
					+ TEXT(" ") + (Scene ? Scene->GetRelativeTransform().ToString() : TEXT("NULL"));
				if (auto* Mesh = Cast<UStaticMeshComponent>(Scene))
					Report += TEXT(" MESH=") + GetPathNameSafe(
						Mesh->GetStaticMesh());
			}
		if (!Apply) continue;
		BP->Modify();
		// Legacy window logic only controls APEX destruction. Keep authored components and valid parameters.
		TArray<UEdGraph*> Graphs;
		BP->GetAllGraphs(Graphs);
		for (auto* Graph : Graphs) if (Graph) FBlueprintEditorUtils::RemoveGraph(BP, Graph, EGraphRemoveFlags::None);
		TArray<FName> InvalidVariables;
		for (const auto& Var : BP->NewVariables)
			if ((Var.VarType.PinCategory == TEXT("object") || Var.VarType.PinCategory == TEXT("class")) && !Var.VarType.
				PinSubCategoryObject.IsValid())
				InvalidVariables.Add(Var.VarName);
		for (auto Var : InvalidVariables) FBlueprintEditorUtils::RemoveMemberVariable(BP, Var);
		if (auto* SCS = BP->SimpleConstructionScript.Get())
		{
			const auto Nodes = SCS->GetAllNodes();
			for (auto* Node : Nodes)
				if (auto* Scene = Cast<USceneComponent>(Node->ComponentTemplate))
					Scene->SetMobility(
						EComponentMobility::Movable);
			// Missing APEX templates are discarded by the loader before SCS enumeration.
			// Fill each surviving frame with ordinary glass in that frame's local bounds.
			for (auto* Node : Nodes)
			{
				auto* Frame = Cast<UStaticMeshComponent>(Node->ComponentTemplate);
				if (!Frame || !Frame->GetStaticMesh() || !Frame->GetStaticMesh()->GetName().StartsWith(
					TEXT("SM_WindowFrame")))
					continue;
				const FName GlassName(*(Node->GetVariableName().ToString() + TEXT("_StaticGlass")));
				if (SCS->FindSCSNode(GlassName)) continue;
				auto* GlassNode = SCS->CreateNode(UStaticMeshComponent::StaticClass(), GlassName);
				auto* Glass = CastChecked<UStaticMeshComponent>(GlassNode->ComponentTemplate);
				Glass->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube")));
				Glass->SetMaterial(0, LoadObject<UMaterialInterface>(
					                   nullptr, TEXT(
						                   "/Game/UVB80/Assets/UVB80/Buildings/Materials/MIC_GlassMaterial_Inst")));
				const FBox Bounds = Frame->GetStaticMesh()->GetBoundingBox();
				FVector Size = Bounds.GetSize() - FVector(8);
				const int32 Thin = Bounds.GetSize().GetAbsMin() == Bounds.GetSize().X
					                   ? 0
					                   : (Bounds.GetSize().Y < Bounds.GetSize().Z ? 1 : 2);
				Size[Thin] = 1.5;
				Glass->SetRelativeLocation(Frame->GetRelativeTransform().TransformPosition(Bounds.GetCenter()));
				Glass->SetRelativeRotation(Frame->GetRelativeRotation());
				Glass->SetRelativeScale3D(Size.ComponentMax(FVector(1)) * Frame->GetRelativeScale3D() / 100.);
				Glass->SetCollisionProfileName(TEXT("BlockAll"));
				Glass->SetMobility(EComponentMobility::Movable);
				SCS->AddNode(GlassNode);
			}
			for (auto* Node : Nodes)
			{
				if (Node->ComponentClass && Node->ComponentTemplate) continue;
				const FString N = Node->GetVariableName().ToString();
				const auto* Scene = Cast<USceneComponent>(Node->ComponentTemplate);
				const FTransform Transform = Scene ? Scene->GetRelativeTransform() : FTransform::Identity;
				SCS->RemoveNodeAndPromoteChildren(Node);
				FString MeshName;
				if (N.Contains(TEXT("ARectangle"))) MeshName = TEXT("SM_GlassFrameARectangle");
				else if (N.Contains(TEXT("ASquare"))) MeshName = TEXT("SM_GlassFrameASquare");
				else if (N.Contains(TEXT("Big"))) MeshName = TEXT("SM_GlassFrameBig");
				if (MeshName.IsEmpty()) continue;
				auto* NewNode = SCS->CreateNode(UStaticMeshComponent::StaticClass(), FName(N + TEXT("_Static")));
				auto* Mesh = CastChecked<UStaticMeshComponent>(NewNode->ComponentTemplate);
				Mesh->SetStaticMesh(
					LoadObject<UStaticMesh>(nullptr, *(TEXT("/Game/UVB80/Assets/UVB80/Buildings/Meshes/") + MeshName)));
				Mesh->SetRelativeTransform(Transform);
				Mesh->SetCollisionProfileName(TEXT("BlockAll"));
				SCS->AddNode(NewNode);
			}
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
		Report += FString::Printf(TEXT("\nSTATUS %d"), int32(BP->Status));
		TArray<UPackage*> Packages{BP->GetOutermost()};
		if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, false)) Report += TEXT(" SAVE_FAILED");
	}
	return Report;
}

bool USovietMilitaryBuilder::BuildNavigation(UObject* Context)
{
	auto* World = GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull);
	if (!World) return false;
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav) return false;
	// Commandlets do not advance the editor ticker that normally clears the async-load lock.
	FlushAsyncLoading();
	FAssetCompilingManager::Get().FinishAllCompilation();
	Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock,
	                               UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
	if (Nav->IsNavigationBuildingLocked(~ENavigationBuildLock::NoUpdateInEditor)) return false;
	Nav->Build();
	if (auto* Data = Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) Data->EnsureBuildCompletion();
	return Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) != nullptr;
}

FVector USovietMilitaryBuilder::ProbeGround(UObject* Context, FVector Position, bool RequireNavigation)
{
	auto* World = GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull);
	const FVector Invalid(0, 0, -1e9);
	if (!World) return Invalid;
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Position + FVector(0, 0, 100), Position - FVector(0, 0, 1200),
	                                     ECC_Visibility) || Hit.ImpactNormal.Z < .7)
		return Invalid;
	FVector Floor = Hit.ImpactPoint;
	if (World->OverlapBlockingTestByChannel(Floor + FVector(0, 0, 98), FQuat::Identity, ECC_Pawn,
	                                        FCollisionShape::MakeCapsule(42, 92)))
		return Invalid;
	if (RequireNavigation)
	{
		auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation Projected;
		if (!Nav || !Nav->ProjectPointToNavigation(Floor, Projected, FVector(80, 80, 100))) return Invalid;
		if (FVector::Dist2D(Floor, Projected.Location) > 60) return Invalid;
	}
	return Floor;
}
