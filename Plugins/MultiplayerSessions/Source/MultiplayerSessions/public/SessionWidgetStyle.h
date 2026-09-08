#pragma once

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

/** 房间浏览器和准备大厅共用的原生 UMG 样式，不依赖额外蓝图资源。 */
namespace SessionWidgetStyle
{
inline UTextBlock* Text(UWidgetTree* Tree, const FString& Value, int32 Size = 18,
    FLinearColor Color = FLinearColor(0.87f, 0.91f, 0.97f))
{
    UTextBlock* Label = Tree->ConstructWidget<UTextBlock>();
    Label->SetText(FText::FromString(Value));
    FSlateFontInfo Font = Label->GetFont();
    Font.Size = Size;
    Label->SetFont(Font);
    Label->SetColorAndOpacity(FSlateColor(Color));
    return Label;
}

inline UButton* Button(UWidgetTree* Tree, FName Name, const FString& Value, UTextBlock** OutLabel = nullptr)
{
    UButton* Result = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
    Result->SetBackgroundColor(FLinearColor(0.08f, 0.40f, 0.64f));
    UTextBlock* Label = Text(Tree, Value);
    Result->AddChild(Label);
    if (OutLabel) *OutLabel = Label;
    return Result;
}

inline UVerticalBox* Frame(UWidgetTree* Tree)
{
    UCanvasPanel* Canvas = Tree->ConstructWidget<UCanvasPanel>();
    Tree->RootWidget = Canvas;
    UBorder* Backdrop = Tree->ConstructWidget<UBorder>();
    Backdrop->SetBrushColor(FLinearColor(0.008f, 0.014f, 0.027f, 0.96f));
    UCanvasPanelSlot* BackdropSlot = Canvas->AddChildToCanvas(Backdrop);
    BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
    BackdropSlot->SetOffsets(FMargin(0.f));
    UScaleBox* Scale = Tree->ConstructWidget<UScaleBox>();
    Scale->SetStretch(EStretch::ScaleToFit);
    UCanvasPanelSlot* ScaleSlot = Canvas->AddChildToCanvas(Scale);
    ScaleSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
    ScaleSlot->SetOffsets(FMargin(32.f));
    USizeBox* Size = Tree->ConstructWidget<USizeBox>();
    Size->SetWidthOverride(960.f);
    Size->SetHeightOverride(620.f);
    Scale->AddChild(Size);
    UBorder* Panel = Tree->ConstructWidget<UBorder>();
    Panel->SetBrushColor(FLinearColor(0.025f, 0.042f, 0.07f));
    Panel->SetPadding(FMargin(28.f));
    Size->AddChild(Panel);
    UVerticalBox* Content = Tree->ConstructWidget<UVerticalBox>();
    Panel->AddChild(Content);
    return Content;
}

inline void Line(UVerticalBox* Box, UWidget* Widget, float BottomPadding = 14.f)
{
    Box->AddChildToVerticalBox(Widget)->SetPadding(FMargin(0.f, 0.f, 0.f, BottomPadding));
}
}
