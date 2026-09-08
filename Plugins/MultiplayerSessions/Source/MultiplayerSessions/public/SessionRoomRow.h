#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SessionRoomRow.generated.h"

class UMenu;
class UButton;
class UHorizontalBox;

UCLASS()
class MULTIPLAYERSESSIONS_API USessionRoomRow : public UUserWidget
{
    GENERATED_BODY()
public:
    void Setup(UMenu* InMenu, int32 InIndex, const FString& Name, const FString& Mode,
        int32 Players, int32 Capacity, int32 Ping, bool bJoinable);
    UButton* GetJoinButton() const { return JoinButton; }
protected:
    virtual bool Initialize() override;
private:
    UFUNCTION()
    void JoinClicked();
    UPROPERTY(Transient)
    TObjectPtr<UButton> JoinButton;
    UPROPERTY(Transient)
    TObjectPtr<UHorizontalBox> Columns;
    TWeakObjectPtr<UMenu> Menu;
    int32 Index = INDEX_NONE;
};
