#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TraversalLadderEntry.h"
#include "ExtractionLadder.generated.h"

class ACharacter;

/** Invisible interaction binding for an existing restored mesh. Local points are feet positions. */
UCLASS(BlueprintType)
class BROKENEDGE_API AExtractionLadder : public AActor
{
	GENERATED_BODY()

public:
	AExtractionLadder();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(EditAnywhere, Replicated)
	FString SourceId;
	UPROPERTY(EditAnywhere, Replicated)
	FVector Bottom = FVector(0, 0, 0);
	UPROPERTY(EditAnywhere, Replicated)
	FVector Top = FVector(0, 0, 250);
	UPROPERTY(EditAnywhere, Replicated)
	FVector BottomExit = FVector(-80, 0, 0);
	UPROPERTY(EditAnywhere, Replicated)
	FVector TopExit = FVector(80, 0, 250);
	UPROPERTY(EditAnywhere, Replicated)
	FTraversalLadderEntry BottomEntry;
	UPROPERTY(EditAnywhere, Replicated)
	FTraversalLadderEntry TopEntry;
	/** Extra rise needed to pass the original ladder rails before crossing the roof edge. */
	UPROPERTY(EditAnywhere, Replicated)
	float TopClearance = 0.f;
	UPROPERTY(EditAnywhere, Replicated)
	FVector GripOrigin = FVector(60, 0, 15);
	UPROPERTY(EditAnywhere, Replicated)
	float RungSpacing = 25.f;
	UPROPERTY(EditAnywhere, Replicated)
	float GripHalfWidth = 22.f;
	UPROPERTY(EditAnywhere, Replicated)
	TArray<FVector> Rungs;
	UPROPERTY(Replicated)
	TObjectPtr<ACharacter> Occupant;
	FVector Point(const FVector& Local) const { return GetActorTransform().TransformPosition(Local); }
	bool Claim(ACharacter* Character);
	void Release(ACharacter* Character);
};
