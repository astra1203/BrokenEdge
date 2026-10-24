#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ExtractionProgressionWidget.generated.h"

UCLASS()
class BROKENEDGE_API UExtractionProgressionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void FocusPanel();
	bool TasksInitially = false;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool Children) override;
	class UExtractionAccountComponent* Account() const;

private:
	TSharedPtr<class SExtractionProgressionPanel> Panel;
};
