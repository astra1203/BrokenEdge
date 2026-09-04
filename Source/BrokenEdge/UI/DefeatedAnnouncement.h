// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DefeatedAnnouncement.generated.h"

/**
 *
 */
UCLASS()
class BROKENEDGE_API UDefeatedAnnouncement : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetDefeatedAnnouncementText(FString AttackerName,FString VictimName);

	/** 公告容器 */
	UPROPERTY(meta = (BindWidget))
	class UHorizontalBox* AnnouncementBox;

	/** 公告文本 */
	UPROPERTY(meta = (BindWidget))
	class UTextBlock* AnnouncementText;

};
