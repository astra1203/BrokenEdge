#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LobbyWidget.generated.h"

UCLASS()
class BROKENEDGE_API ULobbyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Refresh();

protected:
	virtual bool Initialize() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION() void ShopClicked();
	UFUNCTION() void QuestsClicked();
	UPROPERTY(Transient) TObjectPtr<class UButton> ShopButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> QuestsButton;
	UPROPERTY(Transient) TObjectPtr<class UExtractionProgressionWidget> ProgressionWidget;
	UFUNCTION() void StashClicked();
	UFUNCTION() void SquadOneClicked();
	UFUNCTION() void SquadTwoClicked();
	UPROPERTY(Transient) TObjectPtr<class UButton> SquadOne;
	UPROPERTY(Transient) TObjectPtr<class UButton> SquadTwo;
	UPROPERTY(Transient) TObjectPtr<class UButton> StashButton;
	UPROPERTY(Transient) TObjectPtr<class UExtractionStashWidget> StashWidget;
	UFUNCTION()
	void ReadyClicked();
	UFUNCTION()
	void FreeForAllClicked();
	UFUNCTION()
	void TeamsClicked();
	UFUNCTION()
	void ExtractionClicked();
	UPROPERTY(Transient)
	TObjectPtr<class UButton> ExtractionButton;
	UPROPERTY(Transient)
	TObjectPtr<class UButton> FreeForAllButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> TeamsButton;
	UPROPERTY(Transient)
	TObjectPtr<class UTextBlock> RoomInfo;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Status;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ReadyLabel;
	UPROPERTY(Transient)
	TObjectPtr<class UButton> ReadyButton;
	UPROPERTY(Transient)
	TObjectPtr<class UScrollBox> Roster;
	FString LastRoster;
};
