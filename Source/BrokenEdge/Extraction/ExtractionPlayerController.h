#pragma once

#include "CoreMinimal.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "Blueprint/UserWidget.h"
#include "ExtractionItems.h"
#include "ExtractionPlayerController.generated.h"

UCLASS()
class BROKENEDGE_API AExtractionPlayerController : public ABrokenEdgePlayerController
{
	GENERATED_BODY()
	friend class FExtractionDriver;

public:
	virtual void Tick(float Delta) override;
	virtual void SetupInputComponent() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	void BeginExtractionSpectating();
	UFUNCTION(Server, Reliable)
	void ServerCycleSpectator(int32 Direction);
	UFUNCTION(Server, Reliable)
	void ServerLeaveExtraction();
	UPROPERTY(Replicated)
	TObjectPtr<class AExtractionPlayerState> SpectatedPlayer;
	UPROPERTY(Replicated)
	bool bExtractionSpectating = false;
	bool IsMenuOpen() const { return bMenuOpen; }
	void ToggleExtractionMenu();
	class UExtractionInteractionComponent* Interaction() const;
	void Interact();
	void ReleaseInteract();
	void HealAbility();
	void SmokeAbility();
	void ToggleInventory();
	void UseMedical();
	void DropEquippedItem();
	void RequestOpenInventory(class AExtractionLootActor* Target, bool bOpen);
	void RequestItem(FExtractionItemRequest Request);
	UFUNCTION(Server, Reliable)
	void ServerOpenInventory(uint32 Session, class AExtractionLootActor* Target, bool bOpen);
	UFUNCTION(Server, Reliable)
	void ServerItemOperation(FExtractionItemRequest Request);
	UFUNCTION(Server, Reliable)
	void ServerCancelMedical();
	UFUNCTION(Server, Reliable)
	void ServerSwapExtractionWeapons();
	UFUNCTION(Client, Reliable)
	void ClientItemResult(uint32 Sequence, const FString& Error);
	virtual void Destroyed() override;
	FString ItemMessage;
	uint32 ItemResultSequence = 0;

private:
	bool bMenuOpen = false;
	bool bLeaving = false;
	uint32 LocalItemSequence = 0;
	uint32 LocalInventorySession = 0;
	UPROPERTY(Transient)
	TObjectPtr<class UExtractionInventoryWidget> InventoryWidget;
	FTimerHandle LeaveRetryTimer;
	void DestroyLocalSession();
	UPROPERTY(Transient)
	TObjectPtr<class UExtractionWidget> ExtractionWidget;
	UPROPERTY(Transient)
	TObjectPtr<class ACameraActor> SpectatorCamera;
	FVector LastSpectatorLocation = FVector::ZeroVector;
	FRotator LastSpectatorRotation = FRotator::ZeroRotator;
	void SelectSpectatorTarget(int32 Direction);
	UFUNCTION(Client, Reliable)
	void ClientLeaveExtraction();
	UFUNCTION()
	void LocalSessionDestroyed(bool bSuccess);
};

UCLASS()
class BROKENEDGE_API UExtractionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;
	void Refresh();

private:
	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> Status;
	UPROPERTY(Transient) TObjectPtr<class UBorder> SmokeHaze;
	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> Vitals;
	UPROPERTY(Transient)
	TObjectPtr<class UButton> Previous;
	UPROPERTY(Transient)
	TObjectPtr<class UButton> Next;
	UPROPERTY(Transient)
	TObjectPtr<class UButton> Leave;
	UFUNCTION()
	void PreviousClicked();
	UFUNCTION()
	void NextClicked();
	UFUNCTION()
	void LeaveClicked();
};
