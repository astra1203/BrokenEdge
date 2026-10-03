#include "LobbyWidget.h"

#include "BrokenEdge/GameState/LobbyGameState.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "BrokenEdge/PlayerState/BrokenEdgePlayerState.h"
#include "Components/ScrollBox.h"
#include "Engine/World.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "SessionWidgetStyle.h"

bool ULobbyWidget::Initialize()
{
	if (!Super::Initialize()) return false;
	SetIsFocusable(true);
	UVerticalBox* Content = SessionWidgetStyle::Frame(WidgetTree);
	SessionWidgetStyle::Line(Content, SessionWidgetStyle::Text(WidgetTree, TEXT("BROKEN EDGE  /  多人游戏"), 15));
	SessionWidgetStyle::Line(Content, SessionWidgetStyle::Text(WidgetTree, TEXT("准备大厅"), 32));
	RoomInfo = SessionWidgetStyle::Text(WidgetTree, TEXT("正在同步房间信息…"));
	SessionWidgetStyle::Line(Content, RoomInfo);
	UHorizontalBox* Modes = WidgetTree->ConstructWidget<UHorizontalBox>();
	FreeForAllButton = SessionWidgetStyle::Button(WidgetTree, TEXT("FreeForAllButton"), TEXT("FreeForAll · 自由混战"));
	TeamsButton = SessionWidgetStyle::Button(WidgetTree, TEXT("TeamsButton"), TEXT("Teams · 团队对抗"));
	FreeForAllButton->OnClicked.AddUniqueDynamic(this, &ThisClass::FreeForAllClicked);
	TeamsButton->OnClicked.AddUniqueDynamic(this, &ThisClass::TeamsClicked);
	UHorizontalBoxSlot* FreeForAllSlot = Modes->AddChildToHorizontalBox(FreeForAllButton);
	FreeForAllSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	FreeForAllSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
	Modes->AddChildToHorizontalBox(TeamsButton)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	ExtractionButton = SessionWidgetStyle::Button(WidgetTree, TEXT("ExtractionButton"), TEXT("Extraction · 航天行动"));
	ExtractionButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ExtractionClicked);
	Modes->AddChildToHorizontalBox(ExtractionButton)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	ExtractionButton->SetIsEnabled(false);
	FreeForAllButton->SetIsEnabled(false);
	TeamsButton->SetIsEnabled(false);
	SessionWidgetStyle::Line(Content, Modes);
	SessionWidgetStyle::Line(Content, SessionWidgetStyle::Text(WidgetTree,
	                                                           TEXT("未准备的玩家可切换模式 · 切换后所有玩家需要重新准备"), 15));
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	Header->AddChildToHorizontalBox(SessionWidgetStyle::Text(WidgetTree, TEXT("玩家"), 16))->SetSize(
		FSlateChildSize(ESlateSizeRule::Fill));
	USizeBox* StateHeader = WidgetTree->ConstructWidget<USizeBox>();
	StateHeader->SetWidthOverride(160.f);
	StateHeader->AddChild(SessionWidgetStyle::Text(WidgetTree, TEXT("准备状态"), 16));
	Header->AddChildToHorizontalBox(StateHeader);
	SessionWidgetStyle::Line(Content, Header);
	Roster = WidgetTree->ConstructWidget<UScrollBox>();
	Content->AddChildToVerticalBox(Roster)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Status = SessionWidgetStyle::Text(WidgetTree, TEXT("等待玩家准备"));
	Status->SetAutoWrapText(true);
	SessionWidgetStyle::Line(Content, Status);
	UTextBlock* Label = nullptr;
	ReadyButton = SessionWidgetStyle::Button(WidgetTree, TEXT("ReadyButton"), TEXT("准备"), &Label);
	ReadyLabel = Label;
	ReadyButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ReadyClicked);
	SessionWidgetStyle::Line(Content, ReadyButton, 0.f);
	return true;
}

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
		Signature += FString::Printf(TEXT("%d:%s:%d:%d;"), PS->GetPlayerId(), *PS->GetPlayerName(), PS->IsLobbyReady(),
		                             PS == LocalPS);
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
			                                                 PS->GetPlayerName() + (PS == LocalPS
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
	const bool bCanChooseMode = LocalPS && !LocalPS->IsLobbyReady() && !LocalPS->IsInactive()
		&& !LocalPS->IsOnlyASpectator() && !GS->bTravelling && !GS->bModeUpdating && GS->CountdownEndTime <= 0.0;
	const bool bFreeForAll = GS->RoomMatchType == TEXT("FreeForAll");
	const bool bTeams = GS->RoomMatchType == TEXT("Teams");
	FreeForAllButton->SetIsEnabled(bCanChooseMode && !bFreeForAll);
	TeamsButton->SetIsEnabled(bCanChooseMode && !bTeams);
	ExtractionButton->SetIsEnabled(bCanChooseMode && GS->RoomMatchType != TEXT("Extraction"));
	const FLinearColor Selected(0.12f, 0.60f, 0.36f);
	const FLinearColor Unselected(0.08f, 0.40f, 0.64f);
	FreeForAllButton->SetBackgroundColor(bFreeForAll ? Selected : Unselected);
	TeamsButton->SetBackgroundColor(bTeams ? Selected : Unselected);
	ExtractionButton->SetBackgroundColor(GS->RoomMatchType == TEXT("Extraction") ? Selected : Unselected);
	ReadyButton->SetIsEnabled(LocalPS && !GS->bTravelling && !GS->bModeUpdating);
	ReadyLabel->SetText(FText::FromString(LocalPS && LocalPS->IsLobbyReady() ? TEXT("取消准备") : TEXT("准备")));
}
