#include "Menu.h"

#include "MultiplayerSessionsSubsystem.h"
#include "MultiplayerSessions.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"
#include "OnlineSessionSettings.h"

void UMenu::MenuSetup(int32 NumOfPublicConnections, FString TypeOfMatch, FString LobbyPath)
{
	// 重复 Setup 不能覆盖正在执行的请求，也不能重复订阅完成通知。
	if (Request != EMenuRequest::Idle) return;
	NumPublicConnections = FMath::Clamp(NumOfPublicConnections, 1, 64);
	MatchType = TypeOfMatch.TrimStartAndEnd();
	PathToLobby = LobbyPath;
	// 保留 MenuSetup 参数兼容既有蓝图；客户端不再创建监听服务器。
	if (!IsInViewport()) AddToViewport();
	SetVisibility(ESlateVisibility::Visible);
	SetIsFocusable(true);

	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		FInputModeUIOnly InputModeData;
		InputModeData.SetWidgetToFocus(TakeWidget());
		InputModeData.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PlayerController->SetInputMode(InputModeData);
		PlayerController->SetShowMouseCursor(true);
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		MultiplayerSessionsSubsystem = GameInstance->GetSubsystem<UMultiplayerSessionsSubsystem>();
	}
	if (MultiplayerSessionsSubsystem)
	{
		MultiplayerSessionsSubsystem->MultiplayerOnFindSessionsComplete.RemoveAll(this);
		MultiplayerSessionsSubsystem->MultiplayerOnFindSessionsComplete.AddUObject(this, &ThisClass::OnFindSessions);
		MultiplayerSessionsSubsystem->MultiplayerOnJoinSessionComplete.RemoveAll(this);
		MultiplayerSessionsSubsystem->MultiplayerOnJoinSessionComplete.AddUObject(this, &ThisClass::OnJoinSession);
		MultiplayerSessionsSubsystem->MultiplayerOnDestorySessionComplete.AddUniqueDynamic(this, &ThisClass::OnDestorySession);
	}
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::OnNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::OnTravelFailure);
	}
	if (HostButton) HostButton->SetVisibility(ESlateVisibility::Collapsed);
	if (JoinButton)
	{
		if (UTextBlock* Label = Cast<UTextBlock>(JoinButton->GetChildAt(0))) Label->SetText(FText::FromString(TEXT("加入服务器")));
	}
	SetStatus(MultiplayerSessionsSubsystem ? TEXT("请选择加入服务器") : TEXT("在线会话接口不可用"), false);
}

bool UMenu::Initialize()
{
	if (!Super::Initialize()) return false;
	if (HostButton) HostButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HostButtonClicked);
	if (JoinButton) JoinButton->OnClicked.AddUniqueDynamic(this, &ThisClass::JoinButtonClicked);
	return true;
}

void UMenu::NativeDestruct()
{
	// NativeDestruct 已处于移除流程；这里不能再次 RemoveFromParent。
	MenuTearDown();
	Super::NativeDestruct();
}

void UMenu::SetStatus(const FString& Message, bool bBusy)
{
	if (HostButton) HostButton->SetIsEnabled(false);
	if (JoinButton) JoinButton->SetIsEnabled(!bBusy && MultiplayerSessionsSubsystem != nullptr);
	if (StatusText) StatusText->SetText(FText::FromString(Message));
	UE_LOG(LogMultiplayerSessions, Log, TEXT("Menu: %s"), *Message);
	if (GEngine) GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 8.f, bBusy ? FColor::Cyan : FColor::Yellow, Message);
}

void UMenu::HostButtonClicked()
{
	// 保留反射函数兼容旧蓝图绑定，专服由独立进程启动。
	SetStatus(TEXT("请启动专用服务器后点击加入服务器"), false);
}

void UMenu::OnCreateSession(bool bWasSuccessful)
{
	// 客户端菜单不订阅建房完成事件，也不执行 ServerTravel。
}

void UMenu::JoinButtonClicked()
{
	if (Request != EMenuRequest::Idle || !MultiplayerSessionsSubsystem || MultiplayerSessionsSubsystem->IsBusy()) return;
	if (MultiplayerSessionsSubsystem->HasSession())
	{
		bSearchAfterCleanup = true;
		CleanUpSession(TEXT("正在清理旧会话…"));
		return;
	}
	Request = EMenuRequest::Find;
	SetStatus(TEXT("正在搜索房间…"), true);
	MultiplayerSessionsSubsystem->FindSessions(10000);
}

void UMenu::OnFindSessions(const TArray<FOnlineSessionSearchResult>& SessionResults, bool bWasSuccessful)
{
	if (Request != EMenuRequest::Find) return;
	if (bWasSuccessful && MultiplayerSessionsSubsystem)
	{
		for (const FOnlineSessionSearchResult& Result : SessionResults)
		{
			FString FoundMatchType;
			FString ProjectName;
			Result.Session.SessionSettings.Get(FName(TEXT("MatchType")), FoundMatchType);
			Result.Session.SessionSettings.Get(FName(TEXT("ProjectName")), ProjectName);
			// 跳过其他项目、模式不符、无效及已满房间。
			if (Result.IsValid() && FoundMatchType == MatchType && ProjectName == FApp::GetProjectName()
				&& Result.Session.SessionSettings.bIsDedicated && Result.Session.NumOpenPublicConnections > 0)
			{
				Request = EMenuRequest::Join;
				SetStatus(TEXT("找到匹配房间，正在加入…"), true);
				MultiplayerSessionsSubsystem->JoinSession(Result);
				return;
			}
		}
	}
	Request = EMenuRequest::Idle;
	SetStatus(bWasSuccessful ? TEXT("没有可加入的同模式房间，请稍后重试") : TEXT("搜索失败，请重试"), false);
}

void UMenu::OnJoinSession(EOnJoinSessionCompleteResult::Type Result)
{
	if (Request != EMenuRequest::Join) return;
	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		UE_LOG(LogMultiplayerSessions, Warning, TEXT("Menu join failed: Result=%d"), static_cast<int32>(Result));
		CleanUpSession(TEXT("加入房间失败，房间可能已满或关闭"));
		return;
	}
	FString Address;
	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController || !MultiplayerSessionsSubsystem || !MultiplayerSessionsSubsystem->GetResolvedConnectString(Address))
	{
		CleanUpSession(TEXT("无法取得服务器连接地址，请重试"));
		return;
	}
	Request = EMenuRequest::Travel;
	SetStatus(TEXT("正在连接服务器…"), true);
	UE_LOG(LogMultiplayerSessions, Log, TEXT("Menu ClientTravel: %s"), *Address);
	PlayerController->ClientTravel(Address, TRAVEL_Absolute);
}

void UMenu::CleanUpSession(const FString& Message)
{
	Request = EMenuRequest::Cleanup;
	SetStatus(Message, true);
	if (MultiplayerSessionsSubsystem) MultiplayerSessionsSubsystem->DestorySession();
	else OnDestorySession(false);
}

void UMenu::OnDestorySession(bool bWasSuccessful)
{
	if (Request != EMenuRequest::Cleanup) return;
	const bool bSearch = bSearchAfterCleanup && bWasSuccessful;
	bSearchAfterCleanup = false;
	Request = EMenuRequest::Idle;
	if (bSearch) JoinButtonClicked();
	else SetStatus(bWasSuccessful ? TEXT("会话已清理，可以重新加入服务器") : TEXT("清理会话失败，请重试"), false);
}

void UMenu::OnNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& Error)
{
	if (World == GetWorld() && Request == EMenuRequest::Travel)
	{
		UE_LOG(LogMultiplayerSessions, Warning, TEXT("Connection failed: %s"), *Error);
		CleanUpSession(TEXT("连接失败，请重试"));
	}
}

void UMenu::OnTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& Error)
{
	if (World == GetWorld() && Request == EMenuRequest::Travel)
	{
		UE_LOG(LogMultiplayerSessions, Warning, TEXT("Travel failed: %s"), *Error);
		CleanUpSession(TEXT("地图跳转失败，请重试"));
	}
}

void UMenu::MenuTearDown()
{
	if (MultiplayerSessionsSubsystem)
	{
		MultiplayerSessionsSubsystem->MultiplayerOnFindSessionsComplete.RemoveAll(this);
		MultiplayerSessionsSubsystem->MultiplayerOnJoinSessionComplete.RemoveAll(this);
		MultiplayerSessionsSubsystem->MultiplayerOnDestorySessionComplete.RemoveDynamic(this, &ThisClass::OnDestorySession);
	}
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	NetworkFailureHandle.Reset();
	TravelFailureHandle.Reset();
	Request = EMenuRequest::Idle;
	bSearchAfterCleanup = false;
	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		PlayerController->SetInputMode(FInputModeGameOnly());
		PlayerController->SetShowMouseCursor(false);
	}
}
