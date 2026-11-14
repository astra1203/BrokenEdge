#pragma once
#include "CoreMinimal.h"
class UTextBlock;

namespace ExtractionHUDMetrics
{
    struct FSample
    {
        TArray<double> RefreshMilliseconds;
        int64 TextAttempts = 0;
        int64 TextWrites = 0;
    };
    // Collection is opt-in for bounded game-thread captures; normal play keeps no samples.
    void Begin();
    FSample Finish();
    void SetText(UTextBlock* Widget, const FText& Text);
    class FRefreshScope
    {
    public:
        FRefreshScope();
        ~FRefreshScope();
    private:
        double Started = 0;
    };
}
