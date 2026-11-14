#include "BrokenEdge/Extraction/Map/ExtractionRegionStream.h"
#include "BrokenEdge/Extraction/Core/ExtractionPlayerController.h"
#include "BrokenEdge/Extraction/Core/ExtractionState.h"
#include "Components/SceneComponent.h"
#include "Engine/LevelStreaming.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ProfilingDebugging/CsvProfiler.h"
// 区域流送管理器：服务器常驻该区域，客户端按玩家、观战目标及相机到区域的距离异步加载。
// 加载进内存和关卡可见是两个阶段；进入区域前还需确认该客户端已报告可见，防止踩进未加载碰撞。

// 开启逐帧管理并创建根组件；各世界实例独立管理本地流送，不通过复制 Actor 请求加载。
AExtractionRegionStream::AExtractionRegionStream()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Region")));
}

// 查找世界中的首个区域管理器，没配置时返回空，调用方可按无区域门槛处理。
AExtractionRegionStream* AExtractionRegionStream::Find(const UWorld* World)
{
	if (World) for (TActorIterator<AExtractionRegionStream> It(World); It; ++It) return *It;
	return nullptr;
}

// 按软引用资产名查找当前世界的流送关卡对象，不在此创建或阻塞加载。
ULevelStreaming* AExtractionRegionStream::Streaming() const
{
	if (!GetWorld() || RegionLevel.IsNull()) return nullptr;
	const FString Name = RegionLevel.GetAssetName();
	for (ULevelStreaming* Level : GetWorld()->GetStreamingLevels())
		if (Level && Level->GetWorldAssetPackageName().EndsWith(Name)) return Level;
	return nullptr;
}

// 仅关卡已可见才算就绪，IsLoaded 为真仍可能尚未挂入世界。
bool AExtractionRegionStream::IsReady() const
{
	const auto* Level = Streaming();
	return Level && Level->IsLevelVisible();
}

// 报告资源已加载状态，供统计和诊断使用，不等同于可安全进入。
bool AExtractionRegionStream::IsLoaded() const
{
	const auto* Level = Streaming();
	return Level && Level->IsLevelLoaded();
}

// 计算点到区域包围盒的水平距离，单位厘米，区域内部水平距离为零。
float AExtractionRegionStream::Distance(const FVector& Point) const
{
	return FVector::Dist2D(Point, FBox(RegionMin, RegionMax).GetClosestPointTo(Point));
}

// 远离预加载范围时无需等待该区域；附近视角必须等到关卡可见。
bool AExtractionRegionStream::IsViewReady(const FVector& Location) const
{
	return Distance(Location) > PreloadDistance || IsReady();
}

// 服务器自身就绪后，再检查网络连接记录的客户端可见关卡；本地控制器无连接时不额外等待。
bool AExtractionRegionStream::ClientReady(APlayerController* PC) const
{
	if (!IsReady()) return false;
	const auto* Connection = PC ? PC->GetNetConnection() : nullptr;
	const auto* Level = Streaming();
	return !Connection || (Level && Connection->ClientVisibleLevelNames.Contains(Level->GetWorldAssetPackageFName()));
}

// 保存区域外安全位置；客户端未就绪却进入区域时退回该位置并停止移动。
void AExtractionRegionStream::ProtectBoundary(APlayerController* PC, bool bReady)
{
	APawn* Pawn = PC ? PC->GetPawn().Get() : nullptr;
	if (!Pawn) return;
	const FVector Position = Pawn->GetActorLocation();
	if (!FBox(RegionMin, RegionMax).IsInsideOrOn(Position))
	{
		SafePositions.Add(Pawn, Position);
		return;
	}
	if (bReady) return;
	if (PC->IsLocalController()) bBlocked = true;
	if (const auto* Safe = SafePositions.Find(Pawn))
	{
		Pawn->SetActorLocation(*Safe, false, nullptr, ETeleportType::TeleportPhysics);
		if (auto* Character = Cast<ACharacter>(Pawn)) Character->GetCharacterMovement()->StopMovementImmediately();
	}
}

// 客户端采用预加载/卸载两档距离与延迟形成滞回，避免边界来回移动频繁加载。
// 提交异步加载可见请求后记录状态变化，再在服务器和本地执行边界保护。
void AExtractionRegionStream::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	auto* Level = Streaming();
	if (!Level) return;
	const double Now = GetWorld()->GetTimeSeconds();
	bBlocked = false;
	const bool Authority = GetNetMode() != NM_Client;
	bool Want = Authority;
	auto* Local = Cast<AExtractionPlayerController>(GetWorld()->GetFirstPlayerController());
	if (Local && Local->IsLocalController() && !Authority)
	{
		float Nearest = TNumericLimits<float>::Max();
		if (const APawn* Pawn = Local->GetPawn()) Nearest = Distance(Pawn->GetActorLocation());
		if (Local->bExtractionSpectating && Local->SpectatedPlayer && Local->SpectatedPlayer->GetPawn())
			Nearest = FMath::Min(Nearest, Distance(Local->SpectatedPlayer->GetPawn()->GetActorLocation()));
		FVector Camera;
		FRotator Rotation;
		Local->GetPlayerViewPoint(Camera, Rotation);
		Nearest = FMath::Min(Nearest, Distance(Camera));
		if (Nearest <= PreloadDistance)
		{
			Want = true;
			OutsideSince = -1;
		}
		else if (Nearest <= UnloadDistance)
		{
			Want = bRequested;
			OutsideSince = -1;
		}
		else
		{
			if (OutsideSince < 0) OutsideSince = Now;
			Want = bRequested && Now - OutsideSince < UnloadDelay;
		}
	}
	if (Want != bRequested)
	{
		bRequested = Want;
		RequestedAt = Want ? Now : -1;
		CSV_EVENT_GLOBAL(TEXT("NorthRequested=%d"), Want);
		UE_LOG(LogTemp, Display, TEXT("EXTRACTION_REGION Region=%s Requested=%d Time=%.3f"), *RegionId.ToString(), Want,
		       Now);
	}
	const bool Load = Want && (Authority || Now >= DelayUntil);
	Level->bShouldBlockOnLoad = false;
	Level->bShouldBlockOnUnload = false;
	Level->SetShouldBeLoaded(Load);
	Level->SetShouldBeVisible(Load);
	const bool Loaded = IsLoaded(), Visible = IsReady();
	if (Visible && !bWasVisible) ++Loads;
	if (!Loaded && bWasLoaded) ++Unloads;
	if (Loaded != bWasLoaded || Visible != bWasVisible)
	{
		CSV_EVENT_GLOBAL(TEXT("NorthLoaded=%d Visible=%d"), Loaded, Visible);
		UE_LOG(LogTemp, Display, TEXT("EXTRACTION_REGION Region=%s Loaded=%d Visible=%d Time=%.3f Loads=%d Unloads=%d"),
		       *RegionId.ToString(), Loaded, Visible, Now, Loads, Unloads);
	}
	bWasLoaded = Loaded;
	bWasVisible = Visible;
	if (Authority)
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			ProtectBoundary(
				It->Get(), ClientReady(It->Get()));
	else if (Local) ProtectBoundary(Local, Visible);
	for (auto It = SafePositions.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
}

// 仅请求中但尚未可见时显示加载提示；超过 30 秒改为失败提示，不宣称已经取消加载。
FString AExtractionRegionStream::GetStatus() const
{
	if (!bRequested || IsReady()) return FString();
	return RequestedAt >= 0 && GetWorld()->GetTimeSeconds() - RequestedAt >= 30
		       ? TEXT("区域加载失败，可返回原区域或离开对局")
		       : TEXT("区域加载中");
}

// 仅开发自动化测试可延迟客户端加载，用于验证区域不可见时的边界保护。
void AExtractionRegionStream::SetTestLoadDelay(double Seconds)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (GetNetMode() == NM_Client) DelayUntil = GetWorld()->GetTimeSeconds() + FMath::Clamp(Seconds, 0., 60.);
#endif
}

// 清空 Pawn 安全位置缓存，避免跨世界保留失效角色引用。
void AExtractionRegionStream::EndPlay(const EEndPlayReason::Type Reason)
{
	SafePositions.Empty();
	Super::EndPlay(Reason);
}
