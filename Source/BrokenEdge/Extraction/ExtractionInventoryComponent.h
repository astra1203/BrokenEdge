#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ExtractionItems.h"
#include "ExtractionInventoryComponent.generated.h"

UCLASS()
class BROKENEDGE_API UExtractionInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UExtractionInventoryComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(ReplicatedUsing=OnRep_Contents)
	FExtractionContainer Contents;
	void InitializeLoadout();
	/** Called once by raid initialization; returns supplies that must be placed beside the spawn. */
	static FExtractionContainer GrantStartingSupply(FExtractionContainer& Inventory);
	void InitializeGuard();
	void Changed();
	bool SyncWeapons();
	void CaptureWeapons();
	void SyncReserve();
	int32 Reload(class AWeapon* Weapon);
	void RecordShot(class AWeapon* Weapon);
	void DropAll(const FVector& Position);
	void ClearSettled();
	class ABrokenEdgeCharacter* Character() const;
	bool WasDropped() const { return bDropped; }

private:
	bool bInitialized = false;
	TWeakObjectPtr<UWorld> InitializedWorld;
	bool bDropped = false;
	UFUNCTION()
	void OnRep_Contents();
};
