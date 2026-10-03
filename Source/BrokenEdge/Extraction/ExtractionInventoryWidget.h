#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ExtractionInventoryWidget.generated.h"

class SExtractionInventoryPanel;

/** Native, resolution-scaled inventory; all mutations go through production RPCs. */
UCLASS()
class BROKENEDGE_API UExtractionInventoryWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool ReleaseChildren) override;
	void Refresh();
	bool CancelDrag();
private:
	TSharedPtr<SExtractionInventoryPanel> Panel;
};
