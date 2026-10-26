#include "ExtractionNavigation.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "EngineUtils.h"
#include "ExtractionGuard.h"
#include "NavigationPath.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif
// 编辑器准备工具，负责构建导航并逐段检查巡逻路线；运行时或打包版本调用返回 false。
// BlueprintFunctionLibrary 提供静态入口，WorldContext 参数让蓝图/Python 工具提供目标编辑器世界。

// 等待异步资产编译后仅解除 AsyncLoadLock，保留其他导航构建锁；构建后验证每条路线相邻点可完整寻路。
// 成功只证明这里检查的路线段连通，不代表整图所有区域通行已验收。
bool UExtractionNavigation::BuildNavigation(UObject* Context)
{
#if WITH_EDITOR
	auto* World = Context ? Context->GetWorld() : nullptr;
	if (!World || World->WorldType != EWorldType::Editor) return false;
	FNavigationSystem::AddNavigationSystemToWorld(*World, FNavigationSystemRunMode::EditorMode);
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav) return false;
	FlushAsyncLoading();
	FAssetCompilingManager::Get().FinishAllCompilation();
	// The commandlet never runs the delayed editor ticker which releases this lock.
	// All asset tasks are complete here; preserve every unrelated build lock.
	Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock,
	                               UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It) Nav->OnNavigationBoundsUpdated(*It);
	Nav->Build();
	if (!Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) return false;
	bool Valid = true;
	for (TActorIterator<AExtractionPatrolRoute> It(World); It; ++It)
		for (int32 I = 0; I < It->Points.Num(); ++I)
		{
			auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
				World, It->Points[I], It->Points[(I + 1) % It->Points.Num()]);
			const bool Complete = Path && Path->IsValid() && !Path->IsPartial();
			UE_LOG(LogTemp, Display, TEXT("EXTRACTION_NAVIGATION Route=%s Leg=%d Complete=%d"), *It->GetName(), I,
			       Complete);
			Valid &= Complete;
		}
	return Valid;
#else
	return false;
#endif
}
