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
private:
    UFUNCTION()
    void ReadyClicked();
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
