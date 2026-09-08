// 独立进程验收：-game /Game/Maps/Lobby -SessionTestRole=Server|Join|Empty
// -SessionTestMatchType=FreeForAll|Teams -ExecCmds="Automation RunTests MultiplayerSessions.LAN.MenuFlow"
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Button.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "Menu.h"
#include "MultiplayerSessionsSubsystem.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"

namespace SessionMenuTests
{
class FMenuFlow : public IAutomationLatentCommand
{
public:
	FMenuFlow(FAutomationTestBase* InTest, FString InRole, FString InMatchType)
		: Test(InTest), Role(MoveTemp(InRole)), MatchType(MoveTemp(InMatchType)), Started(FPlatformTime::Seconds()) {}

	virtual bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (Phase == 3 && Role != TEXT("Empty"))
		{
			if (Now - StableSince < (Role == TEXT("Server") ? 20.0 : 5.0)) return false;
			Test->AddInfo(FString::Printf(TEXT("SESSION_FLOW_OK Role=%s Map=/Game/Maps/%s"), *Role, *MatchType));
			return true;
		}
		if (Now - Started > 150.0)
		{
			Test->AddError(FString::Printf(TEXT("Session menu timeout: Role=%s Phase=%d"), *Role, Phase));
			return true;
		}
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Game) World = Context.World();
		}
		if (!World || !World->GetGameInstance() || !World->HasBegunPlay()) return false;
		UMultiplayerSessionsSubsystem* Sessions = World->GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>();
		if (!Sessions) return false;

		if (Phase == 0)
		{
			if (Role == TEXT("Server"))
			{
				if (Sessions->IsBusy() || !Sessions->HasSession()) return false;
				Test->TestTrue(TEXT("Server started without local players"), World->GetGameInstance()->GetNumLocalPlayers() == 0);
				Test->TestTrue(TEXT("Server is dedicated"), World->GetNetMode() == NM_DedicatedServer);
				Phase = 2;
				return false;
			}
			TArray<UUserWidget*> Widgets;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, UMenu::StaticClass(), true);
			if (Widgets.Num() != 1) return false;
			Menu = Cast<UMenu>(Widgets[0]);
			Menu->MenuSetup(2, MatchType, TEXT("/Game/Maps/Lobby"));
			Menu->MenuSetup(2, MatchType, TEXT("/Game/Maps/Lobby"));
			UButton* Host = Cast<UButton>(Menu->GetWidgetFromName(TEXT("HostButton")));
			Test->TestTrue(TEXT("Listen hosting is unavailable in client menu"), Host && !Host->GetIsEnabled() && Host->GetVisibility() == ESlateVisibility::Collapsed);
			Sessions->CreateSession(2, MatchType);
			Test->TestFalse(TEXT("Client cannot register a hosted session"), Sessions->HasSession());
			Click(TEXT("JoinButton"));
			Phase = 2;
			return false;
		}
		if (Role == TEXT("Empty"))
		{
			if (Sessions->IsBusy()) return false;
			UButton* Button = Menu.IsValid() ? Cast<UButton>(Menu->GetWidgetFromName(TEXT("JoinButton"))) : nullptr;
			if (!Button || !Button->GetIsEnabled()) return false;
			Test->TestFalse(TEXT("Unmatched search does not create a local session"), Sessions->HasSession());
			if (Phase == 2)
			{
				Click(TEXT("JoinButton"));
				Phase = 3;
				return false;
			}
			Test->TestTrue(TEXT("Search can be retried and Join button recovers"), Button->GetIsEnabled());
			return true;
		}

		const FString Target = TEXT("/Game/Maps/") + MatchType;
		if (World->GetPackage()->GetName() != Target || World->IsInSeamlessTravel()) return false;
		if (Role == TEXT("Server"))
		{
			if (!World->GetGameState() || World->GetGameState()->PlayerArray.Num() != 2) return false;
			const IOnlineSessionPtr Interface = Online::GetSessionInterface(World);
			const FNamedOnlineSession* Session = Interface.IsValid() ? Interface->GetNamedSession(NAME_GameSession) : nullptr;
			if (!Test->TestNotNull(TEXT("Dedicated session survives seamless travel"), Session)) return true;
			FString AdvertisedMatch;
			Session->SessionSettings.Get(FName(TEXT("MatchType")), AdvertisedMatch);
			Test->TestEqual(TEXT("Server advertises configured mode"), AdvertisedMatch, MatchType);
			Test->TestEqual(TEXT("Server uses configured capacity"), Session->SessionSettings.NumPublicConnections, 2);
			Test->TestTrue(TEXT("Session started"), Session->SessionState == EOnlineSessionState::InProgress);
			Test->TestTrue(TEXT("Server remains dedicated after travel"), World->GetNetMode() == NM_DedicatedServer);
			Test->TestTrue(TEXT("Session advertises a dedicated server"), Session->SessionSettings.bIsDedicated);
			const AGameModeBase* GameMode = World->GetAuthGameMode();
			Test->TestTrue(TEXT("GameSession enforces configured capacity after travel"),
				GameMode && GameMode->GameSession && GameMode->GameSession->MaxPlayers == 2);
		}
		else
		{
			if (!World->GetGameState() || World->GetGameState()->PlayerArray.Num() != 2) return false;
			Test->TestTrue(TEXT("Join/ClientTravel reached a network client world"), World->GetNetMode() == NM_Client);
			Test->TestTrue(TEXT("Joined session exists"), Sessions->HasSession());
		}
		StableSince = Now;
		Phase = 3;
		return false;
	}

private:
	void Click(FName ButtonName)
	{
		UButton* Button = Menu.IsValid() ? Cast<UButton>(Menu->GetWidgetFromName(ButtonName)) : nullptr;
		if (Test->TestNotNull(*ButtonName.ToString(), Button)) Button->OnClicked.Broadcast();
	}
	FAutomationTestBase* Test;
	FString Role;
	FString MatchType;
	TWeakObjectPtr<UMenu> Menu;
	double Started;
	double StableSince = 0.0;
	int32 Phase = 0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSessionMenuFlowTest, "MultiplayerSessions.LAN.MenuFlow",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter)

bool FSessionMenuFlowTest::RunTest(const FString& Parameters)
{
	FString Role;
	FString MatchType = TEXT("FreeForAll");
	FParse::Value(FCommandLine::Get(), TEXT("SessionTestRole="), Role);
	FParse::Value(FCommandLine::Get(), TEXT("SessionTestMatchType="), MatchType);
	if (Role != TEXT("Server") && Role != TEXT("Join") && Role != TEXT("Empty"))
	{
		AddError(TEXT("Run in standalone -game with -SessionTestRole=Server, Join or Empty."));
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(SessionMenuTests::FMenuFlow(this, Role, MatchType));
	return true;
}
#endif
