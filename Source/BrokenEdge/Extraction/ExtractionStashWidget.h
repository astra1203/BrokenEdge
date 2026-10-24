#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ExtractionStashWidget.generated.h"

UCLASS()
class BROKENEDGE_API UExtractionStashWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void FocusPanel();
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool Children) override;
	class UExtractionAccountComponent* Account() const;

private:
	TSharedPtr<class SExtractionStashPanel> Panel;
};
