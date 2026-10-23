#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionMapDefinition.h"
#include "ExtractionExit.generated.h"

UCLASS()
class BROKENEDGE_API AExtractionExit : public AActor
{
	GENERATED_BODY()
public:
	AExtractionExit();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void Configure(const FExtractionExitDefinition& InDefinition);
	void StartRound(double ServerTime);
	bool Available(double ServerTime) const;
	FString UnavailableReason(double ServerTime) const;
	bool Consume();
	UPROPERTY(ReplicatedUsing=OnRep_Definition, BlueprintReadOnly) FExtractionExitDefinition Definition;
	UPROPERTY(Replicated, BlueprintReadOnly) double RoundStartedAt = -1;
	UPROPERTY(Replicated, BlueprintReadOnly) int32 UsedCapacity = 0;
	bool Contains(const FVector& Position) const;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Marker;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UTextRenderComponent> Label;
private:
	UFUNCTION() void OnRep_Definition();
};
