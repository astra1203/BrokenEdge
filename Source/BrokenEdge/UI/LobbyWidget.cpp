#include "LobbyWidget.h"
#include "BrokenEdge/Extraction/ExtractionMapRegistry.h"
#include "Engine/GameInstance.h"
#include "LobbyShowcase.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Engine/TextureRenderTarget2D.h"

#include "BrokenEdge/GameState/LobbyGameState.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "BrokenEdge/PlayerState/BrokenEdgePlayerState.h"
#include "Components/ScrollBox.h"
#include "Engine/World.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "SessionWidgetStyle.h"
#include "BrokenEdge/Extraction/ExtractionAccountComponent.h"
#include "BrokenEdge/Extraction/ExtractionStashWidget.h"
#include "BrokenEdge/Extraction/ExtractionProgressionWidget.h"

bool ULobbyWidget::Initialize()
{
    if (!Super::Initialize()) return false;
    SetIsFocusable(true);
    auto* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
    WidgetTree->RootWidget = Root;
    auto* Backdrop = WidgetTree->ConstructWidget<UBorder>();
    Backdrop->SetBrushColor(FLinearColor(.009f, .016f, .018f));
    auto* BackSlot = Root->AddChildToCanvas(Backdrop);
    BackSlot->SetAnchors(FAnchors(0,0,1,1)); BackSlot->SetOffsets(FMargin(0));
    auto* Scale = WidgetTree->ConstructWidget<UScaleBox>();
    Scale->SetStretch(EStretch::ScaleToFit);
    auto* ScaleSlot = Root->AddChildToCanvas(Scale);
    ScaleSlot->SetAnchors(FAnchors(0,0,1,1)); ScaleSlot->SetOffsets(FMargin(20));
    auto* Size = WidgetTree->ConstructWidget<USizeBox>();
    Size->SetWidthOverride(1280); Size->SetHeightOverride(720); Scale->AddChild(Size);
    auto* Stage = WidgetTree->ConstructWidget<UCanvasPanel>(); Size->AddChild(Stage);
    auto Place = [&](UWidget* W, FVector2D P, FVector2D S)
    {
        auto* LayoutSlot = Stage->AddChildToCanvas(W); LayoutSlot->SetPosition(P); LayoutSlot->SetSize(S);
    };
    Place(SessionWidgetStyle::Text(WidgetTree,TEXT("烽火地带"),32),FVector2D(0,8),FVector2D(400,45));
    Wallet = SessionWidgetStyle::Text(WidgetTree,TEXT("正在同步仓库…"),17);
    Wallet->SetJustification(ETextJustify::Right);
    Place(Wallet,FVector2D(740,20),FVector2D(540,35));
    auto* Modes = WidgetTree->ConstructWidget<UHorizontalBox>();
    FreeForAllButton = SessionWidgetStyle::Button(WidgetTree,TEXT("FreeForAllButton"),TEXT("自由混战"));
    TeamsButton = SessionWidgetStyle::Button(WidgetTree,TEXT("TeamsButton"),TEXT("团队对抗"));
    ExtractionButton = SessionWidgetStyle::Button(WidgetTree,TEXT("ExtractionButton"),TEXT("烽火地带 · 搜打撤"));
    FreeForAllButton->OnClicked.AddUniqueDynamic(this,&ThisClass::FreeForAllClicked);
    TeamsButton->OnClicked.AddUniqueDynamic(this,&ThisClass::TeamsClicked);
    ExtractionButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ExtractionClicked);
    for (auto* B : {ExtractionButton.Get(),FreeForAllButton.Get(),TeamsButton.Get()})
    {
        auto* LayoutSlot = Modes->AddChildToHorizontalBox(B); LayoutSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        LayoutSlot->SetPadding(FMargin(0,0,8,0)); B->SetIsEnabled(false);
    }
    Place(Modes,FVector2D(0,68),FVector2D(1280,42));
    auto* Hero = WidgetTree->ConstructWidget<UBorder>(); Hero->SetPadding(FMargin(0));
    Hero->SetBrushColor(FLinearColor(.015f,.028f,.03f));
    Portrait = WidgetTree->ConstructWidget<UImage>(); Hero->AddChild(Portrait);
    Place(Hero,FVector2D(0,124),FVector2D(800,440));
    Place(SessionWidgetStyle::Text(WidgetTree,TEXT("行动小队  /  蜂医"),18),FVector2D(24,142),FVector2D(500,28));
    auto* Content = WidgetTree->ConstructWidget<UVerticalBox>();
    Place(Content,FVector2D(830,124),FVector2D(450,440));
    SessionWidgetStyle::Line(Content,SessionWidgetStyle::Text(WidgetTree,TEXT("行动区域"),14),6);
    MapLabel = SessionWidgetStyle::Text(WidgetTree,TEXT("地图加载中"),26);
    SessionWidgetStyle::Line(Content,MapLabel,6);
    auto* Maps = WidgetTree->ConstructWidget<UHorizontalBox>();
    SpaceCenterButton = SessionWidgetStyle::Button(WidgetTree,TEXT("SpaceCenterMap"),TEXT("航天基地"));
    SovietMilitaryButton = SessionWidgetStyle::Button(WidgetTree,TEXT("SovietMilitaryMap"),TEXT("苏联军事基地"));
    SpaceCenterButton->OnClicked.AddUniqueDynamic(this,&ThisClass::SpaceCenterClicked);
    SovietMilitaryButton->OnClicked.AddUniqueDynamic(this,&ThisClass::SovietMilitaryClicked);
    for (auto* B : {SpaceCenterButton.Get(), SovietMilitaryButton.Get()})
        Maps->AddChildToHorizontalBox(B)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    SessionWidgetStyle::Line(Content,Maps,8);
    SessionWidgetStyle::Line(Content,SessionWidgetStyle::Text(WidgetTree,TEXT("搜索物资 · 完成任务 · 携装撤离"),16),18);
    RoomInfo = SessionWidgetStyle::Text(WidgetTree,TEXT("正在同步房间…"),14);
    RoomInfo->SetAutoWrapText(true); SessionWidgetStyle::Line(Content,RoomInfo,12);
    auto* Squads = WidgetTree->ConstructWidget<UHorizontalBox>();
    SquadOne = SessionWidgetStyle::Button(WidgetTree,TEXT("SquadOne"),TEXT("加入一队"));
    SquadTwo = SessionWidgetStyle::Button(WidgetTree,TEXT("SquadTwo"),TEXT("加入二队"));
    SquadOne->OnClicked.AddUniqueDynamic(this,&ThisClass::SquadOneClicked);
    SquadTwo->OnClicked.AddUniqueDynamic(this,&ThisClass::SquadTwoClicked);
    for (auto* B : {SquadOne.Get(),SquadTwo.Get()}) Squads->AddChildToHorizontalBox(B)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    SessionWidgetStyle::Line(Content,Squads,12);
    Roster = WidgetTree->ConstructWidget<UScrollBox>();
    Content->AddChildToVerticalBox(Roster)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Status = SessionWidgetStyle::Text(WidgetTree,TEXT("等待玩家准备"),16);
    Status->SetAutoWrapText(true); Place(Status,FVector2D(0,578),FVector2D(1280,46));
    auto* Services = WidgetTree->ConstructWidget<UHorizontalBox>();
    StashButton = SessionWidgetStyle::Button(WidgetTree,TEXT("StashButton"),TEXT("仓库 / 出战配装"));
    ShopButton = SessionWidgetStyle::Button(WidgetTree,TEXT("ShopButton"),TEXT("商店"));
    QuestsButton = SessionWidgetStyle::Button(WidgetTree,TEXT("QuestsButton"),TEXT("任务 / 成长"));
    StashButton->OnClicked.AddUniqueDynamic(this,&ThisClass::StashClicked);
    ShopButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ShopClicked);
    QuestsButton->OnClicked.AddUniqueDynamic(this,&ThisClass::QuestsClicked);
    for (auto* B : {StashButton.Get(),ShopButton.Get(),QuestsButton.Get()})
    {
        auto* LayoutSlot=Services->AddChildToHorizontalBox(B); LayoutSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        LayoutSlot->SetPadding(FMargin(0,0,8,0)); B->SetBackgroundColor(FLinearColor(.035f,.065f,.068f));
    }
    Place(Services,FVector2D(0,635),FVector2D(800,65));
    UTextBlock* Label=nullptr;
    ReadyButton=SessionWidgetStyle::Button(WidgetTree,TEXT("ReadyButton"),TEXT("准备出战"),&Label);
    ReadyLabel=Label; ReadyButton->SetBackgroundColor(FLinearColor(.16f,.7f,.48f));
    ReadyButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ReadyClicked);
    Place(ReadyButton,FVector2D(830,635),FVector2D(450,65));
    return true;
}

void ULobbyWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (!Showcase && GetWorld() && GetWorld()->GetNetMode()!=NM_DedicatedServer)
    {
        Showcase=GetWorld()->SpawnActor<ALobbyShowcase>(ALobbyShowcase::StaticClass(),FVector(0,0,-100000),FRotator::ZeroRotator);
        if (Showcase && Showcase->Portrait)
        {
            FSlateBrush Brush; Brush.SetResourceObject(Showcase->Portrait); Brush.ImageSize=FVector2D(1600,880);
            Portrait->SetBrush(Brush);
        }
    }
}

void ULobbyWidget::SquadOneClicked() { if (auto* PC = GetOwningPlayer<ABrokenEdgePlayerController>()) PC->ServerSetExtractionSquad(1); }
void ULobbyWidget::SquadTwoClicked() { if (auto* PC = GetOwningPlayer<ABrokenEdgePlayerController>()) PC->ServerSetExtractionSquad(2); }

void ULobbyWidget::ReadyClicked()
{
	ABrokenEdgePlayerController* PC = GetOwningPlayer<ABrokenEdgePlayerController>();
	const ABrokenEdgePlayerState* PS = PC ? PC->GetPlayerState<ABrokenEdgePlayerState>() : nullptr;
	if (PC && PS) PC->ServerSetLobbyReady(!PS->IsLobbyReady());
}

void ULobbyWidget::FreeForAllClicked()
{
	if (ABrokenEdgePlayerController* PC = GetOwningPlayer<ABrokenEdgePlayerController>())
		PC->ServerSetLobbyMatchType(TEXT("FreeForAll"));
}

void ULobbyWidget::TeamsClicked()
{
	if (ABrokenEdgePlayerController* PC = GetOwningPlayer<ABrokenEdgePlayerController>())
		PC->ServerSetLobbyMatchType(TEXT("Teams"));
}

void ULobbyWidget::ExtractionClicked()
{
	if (auto* PC = GetOwningPlayer<ABrokenEdgePlayerController>()) PC->ServerSetLobbyMatchType(TEXT("Extraction"));
}

void ULobbyWidget::Refresh()
{
	const ALobbyGameState* GS = GetWorld() ? GetWorld()->GetGameState<ALobbyGameState>() : nullptr;
	const APlayerController* PC = GetOwningPlayer();
	const ABrokenEdgePlayerState* LocalPS = PC ? PC->GetPlayerState<ABrokenEdgePlayerState>() : nullptr;
	if (!GS) return;
    if (Wallet && PC)
    {
        const auto* AccountView=PC->FindComponentByClass<UExtractionAccountComponent>();
        Wallet->SetText(FText::FromString(AccountView && AccountView->Bound
            ? FString::Printf(TEXT("仓库 %d 件   /   资金 %lld"),AccountView->Profile.Stash.Items.Num(),AccountView->Profile.Progression.Credits)
            : TEXT("正在同步仓库…")));
    }
	const auto* Account = PC ? PC->FindComponentByClass<UExtractionAccountComponent>() : nullptr;
    if (Showcase) { const auto* Held = Account ? Account->Profile.Loadout.InSlot(0) : nullptr; Showcase->SetHeldWeapon(Held ? Held->Definition : NAME_None); }
	StashButton->SetVisibility(GS->RoomMatchType == TEXT("Extraction") ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	StashButton->SetIsEnabled(Account && Account->Bound && !GS->bTravelling);
	for (auto* Service : {ShopButton.Get(), QuestsButton.Get()})
	{
		Service->SetVisibility(StashButton->GetVisibility());
		Service->SetIsEnabled(Account && Account->Bound && !GS->bTravelling);
	}
	if (ProgressionWidget && (GS->bTravelling || GS->RoomMatchType != TEXT("Extraction"))) ProgressionWidget->RemoveFromParent();
	if (StashWidget && (GS->bTravelling || GS->RoomMatchType != TEXT("Extraction"))) StashWidget->RemoveFromParent();
	TArray<ABrokenEdgePlayerState*> Players;
	for (APlayerState* State : GS->PlayerArray)
	{
		if (ABrokenEdgePlayerState* PS = Cast<ABrokenEdgePlayerState>(State))
		{
			if (!PS->IsInactive() && !PS->IsOnlyASpectator()) Players.Add(PS);
		}
	}
	Players.Sort([](const ABrokenEdgePlayerState& A, const ABrokenEdgePlayerState& B)
	{
		return A.GetPlayerId() < B.GetPlayerId();
	});
	FString Signature;
	int32 ReadyCount = 0;
	for (const ABrokenEdgePlayerState* PS : Players)
	{
		Signature += FString::Printf(TEXT("%d:%s:%d:%d:%d;"), PS->GetPlayerId(), *PS->GetPlayerName(), PS->IsLobbyReady(),
		                             PS == LocalPS, GS->RoomMatchType == TEXT("Extraction") ? PS->ExtractionSquad : 0);
		if (PS->IsLobbyReady()) ++ReadyCount;
	}
	if (Signature != LastRoster)
	{
		LastRoster = Signature;
		Roster->ClearChildren();
		for (const ABrokenEdgePlayerState* PS : Players)
		{
			UBorder* Row = WidgetTree->ConstructWidget<UBorder>();
			Row->SetPadding(FMargin(14.f, 12.f));
			Row->SetBrushColor(FLinearColor(0.045f, 0.075f, 0.12f));
			UHorizontalBox* Cells = WidgetTree->ConstructWidget<UHorizontalBox>();
			Row->AddChild(Cells);
			UTextBlock* NameLabel = SessionWidgetStyle::Text(WidgetTree,
			                                                 (GS->RoomMatchType == TEXT("Extraction") ? FString::Printf(TEXT("[%d队] "), PS->ExtractionSquad) : FString()) + PS->GetPlayerName() + (PS == LocalPS
				                                                 ? TEXT("（你）")
				                                                 : TEXT("")), 20);
			NameLabel->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
			NameLabel->SetClipping(EWidgetClipping::ClipToBounds);
			Cells->AddChildToHorizontalBox(NameLabel)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			USizeBox* StateCell = WidgetTree->ConstructWidget<USizeBox>();
			StateCell->SetWidthOverride(146.f);
			StateCell->AddChild(SessionWidgetStyle::Text(WidgetTree, PS->IsLobbyReady() ? TEXT("已准备") : TEXT("未准备"), 20,
			                                             PS->IsLobbyReady()
				                                             ? FLinearColor(0.30f, 0.90f, 0.65f)
				                                             : FLinearColor(0.87f, 0.91f, 0.97f)));
			Cells->AddChildToHorizontalBox(StateCell);
			Roster->AddChild(Row);
		}
	}
	RoomInfo->SetText(FText::FromString(FString::Printf(TEXT("模式：%s    玩家：%d / %d    已准备：%d / %d"),
	                                                    *GS->RoomMatchType, Players.Num(), GS->Capacity, ReadyCount,
	                                                    Players.Num())));
	FString StatusMessage = GS->LobbyStatus;
	if (GS->bTravelling) StatusMessage = TEXT("正在进入游戏…");
	else if (GS->CountdownEndTime > 0.0)
		StatusMessage = FString::Printf(TEXT("全员已准备，%d 秒后开始 · 取消准备可中止倒计时"),
		                                FMath::Clamp(
			                                FMath::CeilToInt(GS->CountdownEndTime - GS->GetServerWorldTimeSeconds()), 0,
			                                3));
	else if (StatusMessage.IsEmpty()) StatusMessage = FString::Printf(TEXT("至少 %d 人，所有玩家准备后自动开始"), GS->MinimumPlayers);
	Status->SetText(FText::FromString(StatusMessage));
	if (GS->RoomMatchType == TEXT("Extraction") && Account)
	{
		if (!Account->Message.IsEmpty()) Status->SetText(FText::FromString(Account->Message));
		else if (!Account->Bound) Status->SetText(FText::FromString(TEXT("正在连接测试档案…")));
		else if (Account->Profile.Result.Outcome != EExtractionOutcome::None)
			Status->SetText(FText::FromString(UExtractionPersistence::OutcomeLabel(Account->Profile.Result.Outcome) + TEXT(" · 可打开仓库查看")));
	}
	const bool bCanChooseMode = LocalPS && !LocalPS->IsLobbyReady() && !LocalPS->IsInactive()
		&& !LocalPS->IsOnlyASpectator() && !GS->bTravelling && !GS->bModeUpdating && GS->CountdownEndTime <= 0.0;
	const bool Extraction = GS->RoomMatchType == TEXT("Extraction");
	const auto* Registry = GetGameInstance()->GetSubsystem<UExtractionMapRegistry>();
	const auto* Map = Registry ? Registry->Find(GS->ExtractionMapId) : nullptr;
	MapLabel->SetText(Extraction ? (Map ? Map->DisplayName : FText::FromString(TEXT("地图不可用"))) : FText::FromString(GS->RoomMatchType));
	SpaceCenterButton->SetVisibility(Extraction ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	SovietMilitaryButton->SetVisibility(SpaceCenterButton->GetVisibility());
	SpaceCenterButton->SetIsEnabled(Extraction && bCanChooseMode && GS->ExtractionMapId != TEXT("SpaceCenter"));
	SovietMilitaryButton->SetIsEnabled(Extraction && bCanChooseMode && GS->ExtractionMapId != TEXT("SovietMilitary"));
	int32 SquadCounts[3] = {};
	for (const auto* P : Players) if (P->ExtractionSquad >= 1 && P->ExtractionSquad <= 2) ++SquadCounts[P->ExtractionSquad];
	SquadOne->SetVisibility(Extraction ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	SquadTwo->SetVisibility(Extraction ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	SquadOne->SetIsEnabled(Extraction && bCanChooseMode && LocalPS->ExtractionSquad != 1 && SquadCounts[1] < 3);
	SquadTwo->SetIsEnabled(Extraction && bCanChooseMode && LocalPS->ExtractionSquad != 2 && SquadCounts[2] < 3);
	if (Showcase) Showcase->SetSquadSize(Extraction && LocalPS && LocalPS->ExtractionSquad>=1 && LocalPS->ExtractionSquad<=2 ? SquadCounts[LocalPS->ExtractionSquad] : 1);
    const bool bFreeForAll = GS->RoomMatchType == TEXT("FreeForAll");
	const bool bTeams = GS->RoomMatchType == TEXT("Teams");
	FreeForAllButton->SetIsEnabled(bCanChooseMode && !bFreeForAll);
	TeamsButton->SetIsEnabled(bCanChooseMode && !bTeams);
	ExtractionButton->SetIsEnabled(bCanChooseMode && GS->RoomMatchType != TEXT("Extraction"));
	const FLinearColor Selected(0.12f, 0.60f, 0.36f);
	const FLinearColor Unselected(0.035f, 0.065f, 0.068f);
	FreeForAllButton->SetBackgroundColor(bFreeForAll ? Selected : Unselected);
	TeamsButton->SetBackgroundColor(bTeams ? Selected : Unselected);
	ExtractionButton->SetBackgroundColor(GS->RoomMatchType == TEXT("Extraction") ? Selected : Unselected);
	ReadyButton->SetIsEnabled(LocalPS && !GS->bTravelling && !GS->bModeUpdating);
	if (GS->RoomMatchType == TEXT("Extraction") && (!Account || !Account->Bound)) ReadyButton->SetIsEnabled(false);
	ReadyLabel->SetText(FText::FromString(LocalPS && LocalPS->IsLobbyReady() ? TEXT("取消准备") : TEXT("准备出战")));
}

void ULobbyWidget::StashClicked()
{
	if (!StashWidget) StashWidget = CreateWidget<UExtractionStashWidget>(GetOwningPlayer());
	if (StashWidget)
	{
		StashWidget->AddToViewport(150);
		StashWidget->FocusPanel();
	}
}

void ULobbyWidget::NativeDestruct()
{
    if (Showcase) { Showcase->Destroy(); Showcase=nullptr; }
	if (ProgressionWidget) ProgressionWidget->RemoveFromParent();
	if (StashWidget) StashWidget->RemoveFromParent();
	Super::NativeDestruct();
}

void ULobbyWidget::ShopClicked()
{
	if (ProgressionWidget) ProgressionWidget->RemoveFromParent();
	ProgressionWidget = CreateWidget<UExtractionProgressionWidget>(GetOwningPlayer());
	ProgressionWidget->AddToViewport(160);
	ProgressionWidget->FocusPanel();
}

void ULobbyWidget::QuestsClicked()
{
	if (ProgressionWidget) ProgressionWidget->RemoveFromParent();
	ProgressionWidget = CreateWidget<UExtractionProgressionWidget>(GetOwningPlayer());
	ProgressionWidget->TasksInitially = true;
	ProgressionWidget->AddToViewport(160);
	ProgressionWidget->FocusPanel();
}

void ULobbyWidget::SpaceCenterClicked()
{
	if (auto* PC = GetOwningPlayer<ABrokenEdgePlayerController>()) PC->ServerSetExtractionMap(TEXT("SpaceCenter"));
}
void ULobbyWidget::SovietMilitaryClicked()
{
	if (auto* PC = GetOwningPlayer<ABrokenEdgePlayerController>()) PC->ServerSetExtractionMap(TEXT("SovietMilitary"));
}
