#pragma once

#include "CoreMinimal.h"
#include "ExtractionTraversalTypes.generated.h"

class AExtractionLadder;

UENUM(BlueprintType)
enum class EExtractionTraversal : uint8 { None, Vault, Mantle, LadderEnter, Ladder, LadderExit };

/** Project tuning, in centimetres/seconds. These are not source-game measurements. */
UCLASS(Config=Game, DefaultConfig)
class BROKENEDGE_API UExtractionTraversalRules : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(Config)
	float VaultHeight = 120.f;
	UPROPERTY(Config)
	float MantleHeight = 180.f;
	UPROPERTY(Config)
	float VaultDepth = 100.f;
	UPROPERTY(Config)
	float Reach = 105.f;
	UPROPERTY(Config)
	float LadderSpeed = 150.f;
	UPROPERTY(Config)
	float LadderReach = 125.f;
	UPROPERTY(Config)
	float LadderRoofReach = 150.f;
	UPROPERTY(Config)
	float VaultSeconds = .85f;
	UPROPERTY(Config)
	float MantleSeconds = 1.25f;
};

/** Also included in CMC corrections so replay starts from authoritative phase/time. */
USTRUCT()
struct FExtractionTraversalState
{
	GENERATED_BODY()
	UPROPERTY()
	EExtractionTraversal Kind = EExtractionTraversal::None;
	UPROPERTY()
	FVector Start = FVector::ZeroVector;
	UPROPERTY()
	FVector Raised = FVector::ZeroVector;
	UPROPERTY()
	FVector Across = FVector::ZeroVector;
	UPROPERTY()
	FVector End = FVector::ZeroVector;
	UPROPERTY()
	FVector Facing = FVector::ForwardVector;
	UPROPERTY()
	FVector Contact = FVector::ZeroVector;
	UPROPERTY()
	TObjectPtr<AExtractionLadder> Ladder;
	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> Support;
	UPROPERTY()
	float Elapsed = 0.f;
	UPROPERTY()
	float Duration = 1.f;
	UPROPERTY()
	float Height = 0.f;
	UPROPERTY()
	uint16 Serial = 0;
};
