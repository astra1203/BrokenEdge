#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Engine/EngineBaseTypes.h"
#include "OnlineSessionSettings.h"
#include "Menu.generated.h"

/** 原生房间列表，保留旧蓝图入口；搜索与点击加入是两个独立操作。 */
UCLASS()
class MULTIPLAYERSESSIONS_API UMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable)
	void MenuSetup(int32 NumOfPublicConnections = 8, FString TypeOfMatch = FString(TEXT("Teams")),
		FString LobbyPath = FString(TEXT("/Game/Maps/Lobby")));

	UFUNCTION(BlueprintCallable, Category = "Sessions")
	void RefreshRooms();
	UFUNCTION(BlueprintCallable, Category = "Sessions")
	void JoinRoom(int32 RoomIndex);
	const TArray<FOnlineSessionSearchResult>& GetRooms() const { return Rooms; }
	bool IsRequestPending() const { return Request != EMenuRequest::Idle; }

protected:
	virtual bool Initialize() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void OnCreateSession(bool bWasSuccessful);
	void OnFindSessions(const TArray<FOnlineSessionSearchResult>& SessionResults, bool bWasSuccessful);
	void OnJoinSession(EOnJoinSessionCompleteResult::Type Result);
	UFUNCTION()
	void OnDestorySession(bool bWasSuccessful);

private:
	UPROPERTY(meta = (BindWidget))
	class UButton* HostButton;
	UPROPERTY(meta = (BindWidget))
	UButton* JoinButton;
	UPROPERTY(meta = (BindWidgetOptional))
	class UTextBlock* StatusText;

	UFUNCTION()
	void HostButtonClicked();
	UFUNCTION()
	void JoinButtonClicked();

	void MenuTearDown();
	void SetStatus(const FString& Message, bool bBusy);
	void CleanUpSession(const FString& Message);
	void RebuildRoomList();
	void OnNetworkFailure(UWorld* World, class UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& Error);
	void OnTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& Error);

	UPROPERTY(Transient)
	class UMultiplayerSessionsSubsystem* MultiplayerSessionsSubsystem = nullptr;
	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
	int32 NumPublicConnections = 8;
	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
	FString MatchType = TEXT("Teams");

	FString PathToLobby;
	TArray<FOnlineSessionSearchResult> Rooms;
	UPROPERTY(Transient)
	class UScrollBox* RoomList = nullptr;
	enum class EMenuRequest : uint8 { Idle, Host, Find, Join, Cleanup, Travel };
	EMenuRequest Request = EMenuRequest::Idle;
	bool bSearchAfterCleanup = false;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
};