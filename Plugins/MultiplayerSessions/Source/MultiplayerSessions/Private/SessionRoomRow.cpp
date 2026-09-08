#include "SessionRoomRow.h"

#include "Menu.h"
#include "SessionWidgetStyle.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"

bool USessionRoomRow::Initialize()
{
    if (!Super::Initialize()) return false;
    UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
    Background->SetBrushColor(FLinearColor(0.045f, 0.075f, 0.12f));
    Background->SetPadding(FMargin(14.f, 12.f));
    WidgetTree->RootWidget = Background;
    Columns = WidgetTree->ConstructWidget<UHorizontalBox>();
    Background->AddChild(Columns);
    return true;
}

void USessionRoomRow::Setup(UMenu* InMenu, int32 InIndex, const FString& Name, const FString& Mode,
    int32 Players, int32 Capacity, int32 Ping, bool bJoinable)
{
    Menu = InMenu;
    Index = InIndex;
    Columns->ClearChildren();
    auto Cell = [this](const FString& Value, float Width)
    {
        UTextBlock* Label = SessionWidgetStyle::Text(WidgetTree, Value, 17);
        Label->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        Label->SetClipping(EWidgetClipping::ClipToBounds);
        Label->SetToolTipText(FText::FromString(Value));
        UHorizontalBoxSlot* Slot = Columns->AddChildToHorizontalBox(Label);
        FSlateChildSize Size(ESlateSizeRule::Fill);
        Size.Value = Width;
        Slot->SetSize(Size);
        Slot->SetVerticalAlignment(VAlign_Center);
        Slot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
    };
    Cell(Name, 3.f);
    Cell(Mode, 1.6f);
    Cell(FString::Printf(TEXT("%d / %d"), Players, Capacity), 1.f);
    Cell(Ping >= 0 ? FString::Printf(TEXT("%d ms"), Ping) : TEXT("—"), 1.f);
    JoinButton = SessionWidgetStyle::Button(WidgetTree, TEXT("JoinRoomButton"), bJoinable ? TEXT("加入房间") : TEXT("不可加入"));
    JoinButton->SetIsEnabled(bJoinable);
    JoinButton->OnClicked.AddUniqueDynamic(this, &ThisClass::JoinClicked);
    USizeBox* Action = WidgetTree->ConstructWidget<USizeBox>();
    Action->SetWidthOverride(110.f);
    Action->AddChild(JoinButton);
    Columns->AddChildToHorizontalBox(Action)->SetVerticalAlignment(VAlign_Center);
}

void USessionRoomRow::JoinClicked()
{
    if (Menu.IsValid()) Menu->JoinRoom(Index);
}
