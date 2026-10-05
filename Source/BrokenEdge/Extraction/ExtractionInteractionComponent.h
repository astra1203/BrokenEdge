#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ExtractionItems.h"
#include "ExtractionInteractionComponent.generated.h"

class AExtractionLootActor;
class UExtractionInventoryComponent;
class ABrokenEdgeCharacter;

UCLASS()
class BROKENEDGE_API UExtractionInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UExtractionInteractionComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* Function) override;
	UPROPERTY(Replicated)
	bool InventoryOpen = false;
	UPROPERTY(Replicated)
	uint32 Session = 0;
	UPROPERTY(Replicated)
	TObjectPtr<AExtractionLootActor> Loot;
	UPROPERTY(Replicated)
	FExtractionContainer LootView;
	UPROPERTY(Replicated)
	double SearchEnd = 0;
	UPROPERTY(Replicated)
	double MedicalEnd = 0;
	UPROPERTY(Replicated)
	double MedicalDuration = 3;
	UPROPERTY(Replicated)
	FGuid MedicalItem;
	UPROPERTY(Transient)
	TObjectPtr<AExtractionLootActor> Focus;
	UPROPERTY(Transient) TObjectPtr<class AExtractionKeyRoom> DoorFocus;
	FString Message;
	bool IsEnabled() const;
	bool CanAct() const;
	bool CanReach(const AExtractionLootActor* Target) const;
	UExtractionInventoryComponent* Inventory() const;
	ABrokenEdgeCharacter* Character() const;
	void Open(uint32 NewSession, AExtractionLootActor* Target, bool bOpen);
	FString Execute(const FExtractionItemRequest& Request);
	void CancelMedical();
	void StopCombatInput();
	void Close();
	void RefreshLootView();
	void DropOnExit();
	FVector DropPosition() const;
	bool IsRevealed(const FGuid& Id) const;

private:
	uint32 LastSequence = 0;
	TMap<FGuid, TSet<FGuid>> Revealed;
	FGuid Searching;
	int32 SeenRevision = -1;
	void UpdateFocus();
	void AdvanceSearch(double Now);
	FString StartMedical(const FGuid& Id);
};
