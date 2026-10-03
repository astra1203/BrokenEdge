#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionItems.h"
#include "ExtractionLootActor.generated.h"

/** Contents stay on the server. Each viewer receives a separately filtered view. */
UCLASS()
class BROKENEDGE_API AExtractionLootActor : public AActor
{
	GENERATED_BODY()

public:
	AExtractionLootActor();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UBoxComponent> InteractionBounds;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class USkeletalMeshComponent> WeaponMesh;
	UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_Appearance)
	bool Loose = false;
	UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_Appearance)
	FName LooseDefinition;
	UPROPERTY(EditAnywhere)
	int32 LooseQuantity = 1;
	UPROPERTY(EditAnywhere, Replicated)
	FString Label = TEXT("物资箱");
	UPROPERTY(Replicated)
	bool Empty = false;
	UPROPERTY(Replicated)
	FGuid ContainerId;
	/** Filled only once by authority, never by opening the UI. */
	UPROPERTY()
	FExtractionContainer Contents;
	UPROPERTY(EditAnywhere)
	bool SeedContents = true;
	void Changed();
	void SetContents(const FExtractionContainer& NewContents);
	FVector InteractionPoint() const;
	static AExtractionLootActor* SpawnContainer(UWorld* World, const FVector& Position, bool bLoose,
	                                            FName Definition = NAME_None);

private:
	UFUNCTION()
	void OnRep_Appearance();
};
