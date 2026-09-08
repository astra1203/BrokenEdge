// 使用 Scripts/Test-LanSessions.ps1，在独立专服和客户端进程中执行真实 LAN 搜索、点击与 RPC。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "BrokenEdge/GameState/LobbyGameState.h"
#include "BrokenEdge/PlayerState/BrokenEdgePlayerState.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "BrokenEdge/UI/LobbyWidget.h"
#include "Menu.h"
#include "SessionRoomRow.h"
#include "MultiplayerSessionsSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

namespace SessionLobbyTests
{
class FMenuFlow : public IAutomationLatentCommand
{
public:
    FMenuFlow(FAutomationTestBase* InTest, FString InRole, FString InMode)
        : Test(InTest), Role(MoveTemp(InRole)), Mode(MoveTemp(InMode)), Started(FPlatformTime::Seconds())
    {
        bSwitchMode = FParse::Param(FCommandLine::Get(), TEXT("SessionTestSwitchMode"));
        TargetMode = bSwitchMode ? (Mode == TEXT("Teams") ? TEXT("FreeForAll") : TEXT("Teams")) : Mode;
    }

    virtual bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        if (Now - Started > 150.0)
        {
            Test->AddError(FString::Printf(TEXT("Session lobby timeout: Role=%s Phase=%d"), *Role, Phase));
            return true;
        }
        UWorld* World = nullptr;
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
            if (Context.WorldType == EWorldType::Game) World = Context.World();
        if (!World || !World->HasBegunPlay() || !World->GetGameInstance()) return false;
        UMultiplayerSessionsSubsystem* Sessions = World->GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>();
        if (!Sessions) return false;

        if (Role == TEXT("Server"))
        {
            if (StableSince > 0.0)
            {
                if (Now - StableSince < 12.0) return false;
                Test->AddInfo(TEXT("SESSION_FLOW_OK Role=Server"));
                return true;
            }
            if (World->GetPackage()->GetName() != TEXT("/Game/Maps/") + TargetMode || World->IsInSeamlessTravel()) return false;
            if (!World->GetGameState() || World->GetGameState()->PlayerArray.Num() != 2) return false;
            if (StableSince == 0.0)
            {
                Test->TestEqual(TEXT("Dedicated server has no local player"), World->GetGameInstance()->GetNumLocalPlayers(), 0);
                Test->TestTrue(TEXT("Server is still dedicated"), World->GetNetMode() == NM_DedicatedServer);
                const IOnlineSessionPtr Interface = Online::GetSessionInterface(World);
                const FNamedOnlineSession* Session = Interface.IsValid() ? Interface->GetNamedSession(NAME_GameSession) : nullptr;
                if (!Test->TestNotNull(TEXT("Session survives travel"), Session)) return true;
                Test->TestTrue(TEXT("Session has started"), Session->SessionState == EOnlineSessionState::InProgress);
                FString AdvertisedMode;
                Session->SessionSettings.Get(FName(TEXT("MatchType")), AdvertisedMode);
                Test->TestEqual(TEXT("Advertised mode agrees with destination map"), AdvertisedMode, TargetMode);
                Test->TestEqual(TEXT("Capacity survives travel"), World->GetAuthGameMode()->GameSession->MaxPlayers, 2);
                StableSince = Now;
            }
            if (Now - StableSince < 12.0) return false;
            Test->AddInfo(TEXT("SESSION_FLOW_OK Role=Server"));
            return true;
        }

        if (Phase == 0)
        {
            TArray<UUserWidget*> Widgets;
            UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, UMenu::StaticClass(), true);
            if (Widgets.Num() != 1 || Sessions->IsBusy()) return false;
            Menu = Cast<UMenu>(Widgets[0]);
            if (Menu->IsRequestPending()) return false;
            Test->TestFalse(TEXT("Browsing never joins automatically"), Sessions->HasSession());
            if (Role == TEXT("Empty"))
            {
                Test->TestEqual(TEXT("Empty search displays no rooms"), Menu->GetRooms().Num(), 0);
                Menu->RefreshRooms();
                Phase = 1;
                return false;
            }
            int32 ExpectedRooms = 1;
            FParse::Value(FCommandLine::Get(), TEXT("SessionTestExpectedRooms="), ExpectedRooms);
            if (Menu->GetRooms().Num() < ExpectedRooms)
            {
                Menu->RefreshRooms();
                return false;
            }
            Test->TestTrue(TEXT("All advertised rooms appear in the list"), Menu->GetRooms().Num() >= ExpectedRooms);
            if (BrowserCaptureTime == 0.0)
            {
                Capture(TEXT("Rooms"));
                BrowserCaptureTime = Now;
                return false;
            }
            if (Now - BrowserCaptureTime < 2.0) return false;
            Menu->JoinRoom(-1);
            Test->TestFalse(TEXT("Invalid selection does not join"), Sessions->HasSession());
            UScrollBox* List = Cast<UScrollBox>(Menu->GetWidgetFromName(TEXT("RoomList")));
            if (!Test->TestNotNull(TEXT("Room list widget"), List)) return true;
            for (int32 Index = 0; Index < Menu->GetRooms().Num(); ++Index)
            {
                FString Match;
                Menu->GetRooms()[Index].Session.SessionSettings.Get(FName(TEXT("MatchType")), Match);
                if (Match != Mode) continue;
                USessionRoomRow* Row = Cast<USessionRoomRow>(List->GetChildAt(Index));
                if (!Test->TestNotNull(TEXT("Clickable room row"), Row)) return true;
                Row->GetJoinButton()->OnClicked.Broadcast();
                Phase = 2;
                return false;
            }
            Test->AddError(TEXT("Requested mode is absent from the room list"));
            return true;
        }
        if (Role == TEXT("Empty"))
        {
            if (Sessions->IsBusy() || !Menu.IsValid() || Menu->IsRequestPending()) return false;
            Test->TestFalse(TEXT("Repeated empty search leaves no session"), Sessions->HasSession());
            Test->AddInfo(TEXT("SESSION_FLOW_OK Role=Empty"));
            return true;
        }

        const FString Target = TEXT("/Game/Maps/") + TargetMode;
        if (World->GetPackage()->GetName() == Target && !World->IsInSeamlessTravel())
        {
            if (!World->GetGameState() || World->GetGameState()->PlayerArray.Num() != 2) return false;
            if (StableSince == 0.0)
            {
                Test->TestTrue(TEXT("Client used selected room and reached its map"), World->GetNetMode() == NM_Client);
                Test->TestTrue(TEXT("Joined session retained"), Sessions->HasSession());
                Test->TestTrue(TEXT("Ready path completed before travel"), Phase >= 4);
                TArray<UUserWidget*> Widgets;
                UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, ULobbyWidget::StaticClass(), true);
                if (Widgets.Num() != 0) return false;
                StableSince = Now;
            }
            if (Now - StableSince < 3.0) return false;
            Test->AddInfo(FString::Printf(TEXT("SESSION_FLOW_OK Role=%s Mode=%s"), *Role, *Mode));
            return true;
        }
        const ALobbyGameState* GS = World->GetGameState<ALobbyGameState>();
        APlayerController* PC = World->GetFirstPlayerController();
        ABrokenEdgePlayerState* PS = PC ? PC->GetPlayerState<ABrokenEdgePlayerState>() : nullptr;
        if (World->GetNetMode() != NM_Client || !GS || !PS) return false;
        TArray<UUserWidget*> Widgets;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, ULobbyWidget::StaticClass(), true);
        if (Widgets.Num() != 1) return false;
        UButton* Ready = Cast<UButton>(Widgets[0]->GetWidgetFromName(TEXT("ReadyButton")));
        if (!Ready || !Ready->GetIsEnabled()) return false;

        if (Phase == 2)
        {
            if (GS->PlayerArray.Num() < 2) return false;
            if (bSwitchMode && !bModeSwitchVerified)
            {
                ABrokenEdgePlayerController* LobbyPC = Cast<ABrokenEdgePlayerController>(PC);
                if (!Test->TestNotNull(TEXT("Lobby controller"), LobbyPC)) return true;
                UButton* ModeButton = Cast<UButton>(Widgets[0]->GetWidgetFromName(
                    TargetMode == TEXT("Teams") ? TEXT("TeamsButton") : TEXT("FreeForAllButton")));
                if (!Test->TestNotNull(TEXT("Mode selection button"), ModeButton)) return true;
                if (ModePhase == 0)
                {
                    if (Role == TEXT("Join"))
                    {
                        Ready->OnClicked.Broadcast();
                        ModePhase = 1;
                    }
                    else
                    {
                        bool bPeerReady = false;
                        for (APlayerState* State : GS->PlayerArray)
                            if (const ABrokenEdgePlayerState* Peer = Cast<ABrokenEdgePlayerState>(State))
                                bPeerReady |= Peer != PS && Peer->IsLobbyReady();
                        if (!bPeerReady) return false;
                        LobbyPC->ServerSetLobbyMatchType(TEXT("InvalidMode"));
                        ModePhase = 1;
                        ModeSince = Now;
                    }
                    return false;
                }
                if (ModePhase == 1)
                {
                    if (Role == TEXT("Join"))
                    {
                        if (!PS->IsLobbyReady()) return false;
                        Test->TestFalse(TEXT("Ready player cannot click mode selection"), ModeButton->GetIsEnabled());
                        LobbyPC->ServerSetLobbyMatchType(TargetMode);
                        ModePhase = 2;
                        ModeSince = Now;
                    }
                    else
                    {
                        if (Now - ModeSince < 3.0) return false;
                        Test->TestEqual(TEXT("Invalid and ready-player requests leave mode unchanged"), GS->RoomMatchType, Mode);
                        Test->TestTrue(TEXT("Unready player can change mode"), ModeButton->GetIsEnabled());
                        ModeButton->OnClicked.Broadcast();
                        ModePhase = 2;
                    }
                    return false;
                }
                if (GS->RoomMatchType != TargetMode || GS->bModeUpdating) return false;
                for (APlayerState* State : GS->PlayerArray)
                    if (const ABrokenEdgePlayerState* Peer = Cast<ABrokenEdgePlayerState>(State))
                        if (Peer->IsLobbyReady()) return false;
                Test->TestEqual(TEXT("Switching mode clears countdown"), GS->CountdownEndTime, 0.0);
                Test->AddInfo(FString::Printf(TEXT("MODE_SWITCH_OK From=%s To=%s AllUnready=1"), *Mode, *TargetMode));
                Capture(TEXT("ModeChanged"));
                bModeSwitchVerified = true;
                PhaseSince = Now;
                return false;
            }
            if (PhaseSince == 0.0)
            {
                Capture(TEXT("Lobby"));
                PhaseSince = Now;
            }
            if (Now - PhaseSince < 2.0) return false;
            Test->TestFalse(TEXT("New player starts unready"), PS->IsLobbyReady());
            Test->TestEqual(TEXT("Enough players alone does not start countdown"), GS->CountdownEndTime, 0.0);
            Ready->OnClicked.Broadcast();
            Phase = 3;
            return false;
        }
        if (Phase == 3 && PS->IsLobbyReady())
        {
            if (Role == TEXT("Cancel"))
            {
                if (GS->CountdownEndTime <= 0.0) return false;
                if (CountdownCaptureTime == 0.0)
                {
                    Capture(TEXT("Countdown"));
                    CountdownCaptureTime = Now;
                    return false;
                }
                if (Now - CountdownCaptureTime < 0.5) return false;
                Ready->OnClicked.Broadcast();
                PhaseSince = Now;
                Phase = 4;
            }
            else Phase = 4;
            return false;
        }
        if (Phase == 4 && Role == TEXT("Cancel"))
        {
            if (PS->IsLobbyReady() || GS->CountdownEndTime != 0.0) return false;
            if (Now - PhaseSince < 5.0) return false;
            Test->TestTrue(TEXT("Cancelling readiness keeps everyone in lobby past original countdown"), World->GetGameState<ALobbyGameState>() != nullptr);
            int32 ReadyPeers = 0;
            for (APlayerState* State : GS->PlayerArray)
                if (const ABrokenEdgePlayerState* Other = Cast<ABrokenEdgePlayerState>(State))
                    if (Other != PS && Other->IsLobbyReady()) ++ReadyPeers;
            Test->TestEqual(TEXT("Other player's readiness replicates"), ReadyPeers, 1);
            Ready->OnClicked.Broadcast();
            Test->AddInfo(TEXT("READY_CANCEL_AND_RETRY_OK"));
            Phase = 5;
        }
        return false;
    }
private:
    void Capture(const FString& Stage) const
    {
        FString Directory;
        if (FParse::Value(FCommandLine::Get(), TEXT("SessionTestScreenshotDir="), Directory))
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory, Role + TEXT("-") + Mode + TEXT("-") + Stage + TEXT(".png")), true, false);
    }
    FAutomationTestBase* Test;
    FString Role;
    FString Mode;
    FString TargetMode;
    bool bSwitchMode = false;
    bool bModeSwitchVerified = false;
    int32 ModePhase = 0;
    double ModeSince = 0.0;
    TWeakObjectPtr<UMenu> Menu;
    double Started;
    double StableSince = 0.0;
    double PhaseSince = 0.0;
    double BrowserCaptureTime = 0.0;
    double CountdownCaptureTime = 0.0;
    int32 Phase = 0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSessionLobbyFlowTest, "MultiplayerSessions.LAN.MenuFlow",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter)

bool FSessionLobbyFlowTest::RunTest(const FString& Parameters)
{
    FString Role;
    FString Mode = TEXT("FreeForAll");
    FParse::Value(FCommandLine::Get(), TEXT("SessionTestRole="), Role);
    FParse::Value(FCommandLine::Get(), TEXT("SessionTestMatchType="), Mode);
    if (Role != TEXT("Server") && Role != TEXT("Join") && Role != TEXT("Cancel") && Role != TEXT("Empty"))
    {
        AddError(TEXT("Run via Scripts/Test-LanSessions.ps1 with an independent server and clients."));
        return false;
    }
    ADD_LATENT_AUTOMATION_COMMAND(SessionLobbyTests::FMenuFlow(this, Role, Mode));
    return true;
}
#endif
