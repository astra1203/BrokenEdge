#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionKeyRoom.generated.h"

UCLASS()
class BROKENEDGE_API AExtractionKeyRoom : public AActor
{
	GENERATED_BODY()

public:
	AExtractionKeyRoom();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UBoxComponent> Barrier;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UStaticMeshComponent> DoorMesh;
	UPROPERTY(EditAnywhere)
	FVector Slide = FVector(180, 0, 0);
	UPROPERTY(EditAnywhere)
	FVector LootOffset = FVector(0, 400, 25);
	UPROPERTY(ReplicatedUsing=OnRep_Door)
	bool Unlocked = false;
	UPROPERTY(ReplicatedUsing=OnRep_Door)
	bool Open = false;
	UPROPERTY(Replicated)
	double OpenAt = 0;
	UPROPERTY(Replicated)
	TObjectPtr<class AExtractionLootActor> Cache;
	UPROPERTY()
	TArray<FGuid> FileLots;
	bool CanReach(class ABrokenEdgeCharacter* Character) const;
	FString Unlock(class UExtractionInteractionComponent* Interaction, int32 InventoryRevision);
	FVector Approach() const;

private:
	FVector ClosedLocation;
	UFUNCTION()
	void OnRep_Door();
};
