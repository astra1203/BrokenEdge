#include "ExtractionRegionStream.h"
#include "ExtractionPlayerController.h"
#include "ExtractionState.h"
#include "Components/SceneComponent.h"
#include "Engine/LevelStreaming.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ProfilingDebugging/CsvProfiler.h"

AExtractionRegionStream::AExtractionRegionStream()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Region")));
}

AExtractionRegionStream* AExtractionRegionStream::Find(const UWorld* World)
{
	if (World) for (TActorIterator<AExtractionRegionStream> It(World); It; ++It) return *It;
	return nullptr;
}

ULevelStreaming* AExtractionRegionStream::Streaming() const
{
	if (!GetWorld() || RegionLevel.IsNull()) return nullptr;
	const FString Name = RegionLevel.GetAssetName();
	for (ULevelStreaming* Level : GetWorld()->GetStreamingLevels())
		if (Level && Level->GetWorldAssetPackageName().EndsWith(Name)) return Level;
	return nullptr;
}

bool AExtractionRegionStream::IsReady() const
{
	const auto* Level = Streaming();
	return Level && Level->IsLevelVisible();
}

bool AExtractionRegionStream::IsLoaded() const
{
	const auto* Level = Streaming();
	return Level && Level->IsLevelLoaded();
}

float AExtractionRegionStream::Distance(const FVector& Point) const
{
	return FVector::Dist2D(Point, FBox(RegionMin, RegionMax).GetClosestPointTo(Point));
}

bool AExtractionRegionStream::IsViewReady(const FVector& Location) const
{
	return Distance(Location) > PreloadDistance || IsReady();
}

bool AExtractionRegionStream::ClientReady(APlayerController* PC) const
{
	if (!IsReady()) return false;
	const auto* Connection = PC ? PC->GetNetConnection() : nullptr;
	const auto* Level = Streaming();
	return !Connection || (Level && Connection->ClientVisibleLevelNames.Contains(Level->GetWorldAssetPackageFName()));
}

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

FString AExtractionRegionStream::GetStatus() const
{
	if (!bRequested || IsReady()) return FString();
	return RequestedAt >= 0 && GetWorld()->GetTimeSeconds() - RequestedAt >= 30
		       ? TEXT("区域加载失败，可返回原区域或离开对局")
		       : TEXT("区域加载中");
}

void AExtractionRegionStream::SetTestLoadDelay(double Seconds)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (GetNetMode() == NM_Client) DelayUntil = GetWorld()->GetTimeSeconds() + FMath::Clamp(Seconds, 0., 60.);
#endif
}

void AExtractionRegionStream::EndPlay(const EEndPlayReason::Type Reason)
{
	SafePositions.Empty();
	Super::EndPlay(Reason);
}
