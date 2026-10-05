#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionRegionStream.generated.h"

/** A bounded client streaming region. Gameplay actors and server navigation remain resident. */
UCLASS()
class BROKENEDGE_API AExtractionRegionStream : public AActor
{
	GENERATED_BODY()
public:
	AExtractionRegionStream();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	static AExtractionRegionStream* Find(const UWorld* World);
	bool IsReady() const;
	bool IsLoaded() const;
	bool IsRequested() const { return bRequested; }
	bool IsViewReady(const FVector& Location) const;
	bool IsBlocked() const { return bBlocked; }
	int32 GetLoads() const { return Loads; }
	int32 GetUnloads() const { return Unloads; }
	FString GetStatus() const;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName RegionId = TEXT("North");
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UWorld> RegionLevel;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector RegionMin = FVector(-5000, 6000, -5000);
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector RegionMax = FVector(7000, 12000, 8000);
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float PreloadDistance = 3000;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float UnloadDistance = 5000;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float UnloadDelay = 5;
	// Only used by Development process-driver tests to exercise a stalled local load.
	void SetTestLoadDelay(double Seconds);

private:
	class ULevelStreaming* Streaming() const;
	float Distance(const FVector& Point) const;
	bool ClientReady(class APlayerController* PC) const;
	void ProtectBoundary(class APlayerController* PC, bool bReady);
	TMap<TWeakObjectPtr<APawn>, FVector> SafePositions;
	double OutsideSince = -1;
	double RequestedAt = -1;
	double DelayUntil = 0;
	bool bRequested = false;
	bool bWasLoaded = false;
	bool bWasVisible = false;
	bool bBlocked = false;
	int32 Loads = 0;
	int32 Unloads = 0;
};
