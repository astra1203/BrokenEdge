#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionSmoke.generated.h"

UCLASS()
class BROKENEDGE_API AExtractionSmoke : public AActor
{
	GENERATED_BODY()

public:
	AExtractionSmoke();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(ReplicatedUsing=Refresh)
	float Radius = 400;
	UPROPERTY(Replicated)
	double ExpiresAt = 0;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UStaticMeshComponent> Cloud;
	static bool Blocks(const UWorld* World, const FVector& Start, const FVector& End);
	UFUNCTION()
	void Refresh();
};
