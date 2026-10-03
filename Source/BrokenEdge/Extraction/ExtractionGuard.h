#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "ExtractionGuard.generated.h"

class ABrokenEdgeCharacter;

UCLASS()
class BROKENEDGE_API AExtractionPatrolRoute : public AActor
{
	GENERATED_BODY()
public:
	AExtractionPatrolRoute();
	/** World-space points, authored and projected to the cooked navigation mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FVector> Points;
};

UENUM()
enum class EExtractionGuardState : uint8 { Patrol, Engage, Search, Return, Dead };

UCLASS()
class BROKENEDGE_API AExtractionGuard : public AAIController
{
	GENERATED_BODY()
public:
	AExtractionGuard();
	virtual void Tick(float Delta) override;
	UPROPERTY() TObjectPtr<AExtractionPatrolRoute> Route;
	UPROPERTY() TObjectPtr<ABrokenEdgeCharacter> Target;
	EExtractionGuardState State = EExtractionGuardState::Patrol;
	int32 PatrolIndex = 0;
	int32 VisibleChecks = 0, Shots = 0, PatrolArrivals = 0;
	bool Sees(const ABrokenEdgeCharacter* Other) const;
private:
	FVector LastKnown = FVector::ZeroVector;
	double LostAt = 0, ReactAt = 0, NextMove = 0;
};
