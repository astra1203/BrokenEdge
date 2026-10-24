#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ExtractionMapDefinition.generated.h"

/** Evidence describes the authored location/rule, not whether runtime tests passed. */
UENUM(BlueprintType)
enum class EExtractionReferenceStatus : uint8 { Temporary, Inferred, Verified };

USTRUCT(BlueprintType)
struct BROKENEDGE_API FExtractionSpawnGroup
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FTransform> Positions;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EExtractionReferenceStatus ReferenceStatus = EExtractionReferenceStatus::Temporary;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Reference;
};

USTRUCT(BlueprintType)
struct BROKENEDGE_API FExtractionExitDefinition
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText Label;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform Transform;
	/** Bounds are relative to Transform; the origin is at ground level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector HalfExtent = FVector(250, 250, 132.5);
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector BoundsOffset = FVector(0, 0, 117.5);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.1"))
	float HoldSeconds = 10;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	float OpensAfterSeconds = 0;
	/** Zero means no close deadline / no capacity limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	float ClosesAfterSeconds = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	int32 Capacity = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EExtractionReferenceStatus ReferenceStatus = EExtractionReferenceStatus::Temporary;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Reference;

	bool IsValid() const
	{
		return !Id.IsNone() && !Transform.ContainsNaN() && !HalfExtent.ContainsNaN() && !BoundsOffset.ContainsNaN()
			&& HalfExtent.GetMin() > 0 && FMath::IsFinite(HoldSeconds) && HoldSeconds >= .1f
			&& FMath::IsFinite(OpensAfterSeconds) && OpensAfterSeconds >= 0
			&& FMath::IsFinite(ClosesAfterSeconds) && (ClosesAfterSeconds == 0 || ClosesAfterSeconds >
				OpensAfterSeconds)
			&& Capacity >= 0;
	}
};

UCLASS(BlueprintType)
class BROKENEDGE_API UExtractionMapDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName MapId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString ReferenceBuild;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform SourceToWorld;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FExtractionSpawnGroup> SpawnGroups;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FExtractionExitDefinition> Exits;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<class UExtractionLootLayout> LootLayout;
};
