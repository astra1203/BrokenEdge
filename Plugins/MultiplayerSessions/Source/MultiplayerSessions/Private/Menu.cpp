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
#include "SessionRoomRow.h"
#include "SessionWidgetStyle.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"

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
		if (UTextBlock* Label = Cast<UTextBlock>(JoinButton->GetChildAt(0))) Label->SetText(FText::FromString(TEXT("刷新房间")));
	}
	SetStatus(MultiplayerSessionsSubsystem ? TEXT("点击刷新搜索房间，再选择要加入的房间") : TEXT("在线会话接口不可用"), false);
	RefreshRooms();
}

bool UMenu::Initialize()
{
	if (!Super::Initialize()) return false;
	// 替换旧蓝图的双按钮布局；现有 MenuSetup 调用无需重新绑定。
	UVerticalBox* Content = SessionWidgetStyle::Frame(WidgetTree);
	SessionWidgetStyle::Line(Content, SessionWidgetStyle::Text(WidgetTree, TEXT("BROKEN EDGE  /  多人游戏"), 15));
	SessionWidgetStyle::Line(Content, SessionWidgetStyle::Text(WidgetTree, TEXT("房间列表"), 32));
	SessionWidgetStyle::Line(Content, SessionWidgetStyle::Text(WidgetTree,
		TEXT("选择房间加入 · 进入大厅后准备 · 全员准备自动开局"), 17));
	JoinButton = SessionWidgetStyle::Button(WidgetTree, TEXT("RefreshRoomsButton"), TEXT("刷新房间"));
	SessionWidgetStyle::Line(Content, JoinButton);
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	auto HeaderCell = [this, Header](const FString& Value, float Weight)
	{
		FSlateChildSize Size(ESlateSizeRule::Fill);
		Size.Value = Weight;
		UHorizontalBoxSlot* Slot = Header->AddChildToHorizontalBox(SessionWidgetStyle::Text(WidgetTree, Value, 16));
		Slot->SetSize(Size);
		Slot->SetPadding(FMargin(14.f, 0.f, 12.f, 0.f));
	};
	HeaderCell(TEXT("房间名称"), 3.f);
	HeaderCell(TEXT("模式"), 1.6f);
	HeaderCell(TEXT("人数"), 1.f);
	HeaderCell(TEXT("延迟"), 1.f);
	USizeBox* ActionHeader = WidgetTree->ConstructWidget<USizeBox>();
	ActionHeader->SetWidthOverride(138.f);
	ActionHeader->AddChild(SessionWidgetStyle::Text(WidgetTree, TEXT("操作"), 16));
	Header->AddChildToHorizontalBox(ActionHeader);
	SessionWidgetStyle::Line(Content, Header);
	RoomList = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("RoomList"));
	Content->AddChildToVerticalBox(RoomList)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	StatusText = SessionWidgetStyle::Text(WidgetTree, TEXT(""), 17);
	StatusText->SetAutoWrapText(true);
	SessionWidgetStyle::Line(Content, StatusText, 0.f);
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
	if (RoomList) RoomList->SetIsEnabled(!bBusy);
	if (StatusText) StatusText->SetText(FText::FromString(Message));
	UE_LOG(LogMultiplayerSessions, Log, TEXT("Menu: %s"), *Message);
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
	RefreshRooms();
}

void UMenu::RefreshRooms()
{
	if (Request != EMenuRequest::Idle || !MultiplayerSessionsSubsystem || MultiplayerSessionsSubsystem->IsBusy()) return;
	if (MultiplayerSessionsSubsystem->HasSession())
	{
		bSearchAfterCleanup = true;
		CleanUpSession(TEXT("正在清理旧会话…"));
		return;
	}
	Request = EMenuRequest::Find;
	Rooms.Reset();
	RebuildRoomList();
	SetStatus(TEXT("正在搜索房间…"), true);
	MultiplayerSessionsSubsystem->FindSessions(10000);
}

void UMenu::OnFindSessions(const TArray<FOnlineSessionSearchResult>& SessionResults, bool bWasSuccessful)
{
	if (Request != EMenuRequest::Find) return;
	Rooms.Reset();
	if (bWasSuccessful && MultiplayerSessionsSubsystem)
	{
		for (const FOnlineSessionSearchResult& Result : SessionResults)
		{
			FString ProjectName;
			Result.Session.SessionSettings.Get(FName(TEXT("ProjectName")), ProjectName);
			// 显示本项目所有模式，满员房间也显示，由行按钮表达是否可加入。
			if (Result.IsValid() && ProjectName == FApp::GetProjectName()
				&& Result.Session.SessionSettings.bIsDedicated)
			{
				Rooms.Add(Result);
			}
		}
	}
	Request = EMenuRequest::Idle;
	RebuildRoomList();
	SetStatus(bWasSuccessful ? (Rooms.IsEmpty() ? TEXT("暂未发现房间，请确认服务器已启动后刷新")
		: FString::Printf(TEXT("找到 %d 个房间，请点击房间右侧的加入按钮"), Rooms.Num()))
		: TEXT("搜索失败，请点击刷新重试"), false);
}

void UMenu::RebuildRoomList()
{
	if (!RoomList) return;
	RoomList->ClearChildren();
	for (int32 Index = 0; Index < Rooms.Num(); ++Index)
	{
		const FOnlineSessionSearchResult& Result = Rooms[Index];
		FString RoomName;
		FString RoomMode;
		Result.Session.SessionSettings.Get(FName(TEXT("RoomName")), RoomName);
		Result.Session.SessionSettings.Get(MultiplayerSessionKeys::MatchType, RoomMode);
		if (RoomName.IsEmpty()) RoomName = Result.Session.OwningUserName;
		if (RoomName.IsEmpty()) RoomName = TEXT("游戏房间");
		RoomName += TEXT(" #") + Result.GetSessionIdStr().Right(6);
		const int32 Capacity = Result.Session.SessionSettings.NumPublicConnections;
		const int32 Players = FMath::Clamp(Capacity - Result.Session.NumOpenPublicConnections, 0, Capacity);
		USessionRoomRow* Row = CreateWidget<USessionRoomRow>(GetOwningPlayer());
		if (!Row) continue;
		Row->Setup(this, Index, RoomName, RoomMode, Players, Capacity, Result.PingInMs,
			Result.Session.NumOpenPublicConnections > 0 && (RoomMode == TEXT("FreeForAll") || RoomMode == TEXT("Teams")));
		RoomList->AddChild(Row);
	}
}

void UMenu::JoinRoom(int32 RoomIndex)
{
	if (Request != EMenuRequest::Idle || !MultiplayerSessionsSubsystem || MultiplayerSessionsSubsystem->IsBusy()
		|| !Rooms.IsValidIndex(RoomIndex)) return;
	const FOnlineSessionSearchResult Selected = Rooms[RoomIndex];
	if (Selected.Session.NumOpenPublicConnections <= 0)
	{
		SetStatus(TEXT("房间已满，请刷新后选择其他房间"), false);
		return;
	}
	if (MultiplayerSessionsSubsystem->HasSession())
	{
		bSearchAfterCleanup = true;
		CleanUpSession(TEXT("正在清理旧会话，请刷新后重新选择房间"));
		return;
	}
	Request = EMenuRequest::Join;
	SetStatus(TEXT("正在加入所选房间…"), true);
	MultiplayerSessionsSubsystem->JoinSession(Selected);
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
