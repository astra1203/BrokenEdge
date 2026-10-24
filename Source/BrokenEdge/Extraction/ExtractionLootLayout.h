#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ExtractionLootLayout.generated.h"

USTRUCT(BlueprintType)
struct BROKENEDGE_API FExtractionLootStack
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Item;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) int32 Quantity = 1;
};

/** Project drop tuning stays separate from source point provenance. */
UCLASS(BlueprintType)
class BROKENEDGE_API UExtractionLootRecipe : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FExtractionLootStack> Items;
};

USTRUCT(BlueprintType)
struct BROKENEDGE_API FExtractionLootPoint
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Id;
	/** Empty means a fixed point. Each named group selects exactly one candidate per raid. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Group;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Weight = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool Enabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform Transform;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText Label;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Kind;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Reference;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<class USkeletalMesh> Appearance;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UExtractionLootRecipe> Recipe;
};

/** One source variant only. Selection is authority-owned and never rerolled by opening a container. */
UCLASS(BlueprintType)
class BROKENEDGE_API UExtractionLootLayout : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Variant;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString GroupPolicy;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FExtractionLootPoint> Points;
	bool Select(int32 Seed, TArray<int32>& Out, FString& Error) const;
	void Spawn(class UWorld* World, int32 Seed) const;
};
