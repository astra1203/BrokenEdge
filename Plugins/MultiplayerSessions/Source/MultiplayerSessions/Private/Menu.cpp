// Fill out your copyright notice in the Description page of Project Settings.


#include "Menu.h"

#include "MultiplayerSessionsSubsystem.h"
#include "Components/Button.h"

void UMenu::MenuSetup(int32 NumOfPublicConnections, FString TypeOfMatch, FString LobbyPath)
{
	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT("初始化菜单"));

	NumPublicConnections = NumOfPublicConnections;
	MatchType = TypeOfMatch;
	PathToLobby = FString::Printf(TEXT("%s?listen"), *LobbyPath);

	// 把自己添加到视口
	AddToViewport();

	SetVisibility(ESlateVisibility::Visible);
	SetFocus();

	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			FInputModeUIOnly InputModeData;
			InputModeData.SetWidgetToFocus(TakeWidget());
			InputModeData.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			PlayerController->SetInputMode(InputModeData);
			PlayerController->SetShowMouseCursor(true);
		}
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		MultiplayerSessionsSubsystem = GameInstance->GetSubsystem<UMultiplayerSessionsSubsystem>();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("BLQ MenuSetup: GameInstance is null!"));
	}

	if (MultiplayerSessionsSubsystem)
	{
		MultiplayerSessionsSubsystem->MultiplayerOnCreateSessionComplete.AddDynamic(this, &ThisClass::OnCreateSession);
		// MultiplayerSessionsSubsystem->MultiplayerOnFindSessionsComplete.AddUObject(this, &ThisClass::OnFindSessions);
		// MultiplayerSessionsSubsystem->MultiplayerOnJoinSessionComplete.AddUObject(this, &ThisClass::OnJoinSession);
		// MultiplayerSessionsSubsystem->MultiplayerOnDestorySessionComplete.AddDynamic(this, &ThisClass::OnDestorySession);
		// MultiplayerSessionsSubsystem->MultiplayerOnStartSessionComplete.AddDynamic(this, &ThisClass::OnStartSession);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("BLQ MenuSetup: MultiplayerSessionsSubsystem is null!"));
	}

}

bool UMenu::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	if (HostButton)
	{
		HostButton->OnClicked.AddDynamic(this, &ThisClass::HostButtonClicked);
	}
	if (JoinButton)
	{
		JoinButton->OnClicked.AddDynamic(this, &ThisClass::JoinButtonClicked);
	}
	return true;
}

void UMenu::NativeDestruct()
{

	MenuTearDown();

	Super::NativeDestruct();
}

void UMenu::OnCreateSession(bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		// if (GEngine)
		// {
		// 	GEngine->AddOnScreenDebugMessage(
		// 		-1,
		// 		15.f,
		// 		FColor::Yellow,
		// 		FString::Printf(TEXT("Session created successful!"))
		// 	);
		// }
		UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
		       TEXT("创建会话成功"));

		if (UWorld* World = GetWorld())
		{
			World->ServerTravel(PathToLobby);
		}
	}

	//@ TODO

}

void UMenu::HostButtonClicked()
{
	HostButton->SetIsEnabled(false);

	if (MultiplayerSessionsSubsystem)
	{
		MultiplayerSessionsSubsystem->CreateSession(NumPublicConnections, MatchType);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("BLQ HostButtonClicked: MultiplayerSessionsSubsystem is null![%s | %s:%d] "),
			TEXT(__FILE__), TEXT(__FUNCTION__), __LINE__);
	}
}

void UMenu::JoinButtonClicked()
{
	JoinButton->SetIsEnabled(false);
	if (MultiplayerSessionsSubsystem)
	{
		MultiplayerSessionsSubsystem->FindSessions(20000);
	}
	else
	{
		JoinButton->SetIsEnabled(true);
	}
}

void UMenu::MenuTearDown()
{
	if (MultiplayerSessionsSubsystem)
	{
		MultiplayerSessionsSubsystem->MultiplayerOnCreateSessionComplete.RemoveDynamic(this, &ThisClass::OnCreateSession);
		MultiplayerSessionsSubsystem->MultiplayerOnFindSessionsComplete.RemoveAll(this);
		MultiplayerSessionsSubsystem->MultiplayerOnJoinSessionComplete.RemoveAll(this);
		// MultiplayerSessionsSubsystem->MultiplayerOnDestorySessionComplete.RemoveDynamic(this, &ThisClass::OnDestorySession);
		// MultiplayerSessionsSubsystem->MultiplayerOnStartSessionComplete.RemoveDynamic(this, &ThisClass::OnStartSession);
	}

	RemoveFromParent();

	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			FInputModeGameOnly InputModeData;
			PlayerController->SetInputMode(InputModeData);
			PlayerController->SetShowMouseCursor(false);
		}
	}


}
