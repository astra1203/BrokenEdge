#include "BrokenEdge/Extraction/UI/ExtractionHUDMetrics.h"
#include "Components/TextBlock.h"

namespace
{
    bool Capturing = false;
    ExtractionHUDMetrics::FSample Sample;
}
void ExtractionHUDMetrics::Begin()
{
    Sample = FSample();
    Sample.RefreshMilliseconds.Reserve(10000);
    Capturing = true;
}
ExtractionHUDMetrics::FSample ExtractionHUDMetrics::Finish()
{
    Capturing = false;
    return MoveTemp(Sample);
}
ExtractionHUDMetrics::FRefreshScope::FRefreshScope()
{
    if (Capturing) Started = FPlatformTime::Seconds();
}
ExtractionHUDMetrics::FRefreshScope::~FRefreshScope()
{
    if (Started > 0 && Capturing) Sample.RefreshMilliseconds.Add((FPlatformTime::Seconds() - Started) * 1000);
}
void ExtractionHUDMetrics::SetText(UTextBlock* Widget, const FText& Text)
{
    if (Capturing) ++Sample.TextAttempts;
    if (Widget->GetText().EqualTo(Text)) return;
    if (Capturing) ++Sample.TextWrites;
    Widget->SetText(Text);
}
