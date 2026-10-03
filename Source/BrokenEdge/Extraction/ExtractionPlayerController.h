#pragma once

#include "CoreMinimal.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "Blueprint/UserWidget.h"
#include "ExtractionPlayerController.generated.h"

UCLASS()
class BROKENEDGE_API AExtractionPlayerController : public ABrokenEdgePlayerController
{
	GENERATED_BODY()

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

private:
	bool bMenuOpen = false;
	bool bLeaving = false;
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
