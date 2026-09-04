// Fill out your copyright notice in the Description page of Project Settings.


#include "DefeatedAnnouncement.h"

#include "Components/TextBlock.h"


void UDefeatedAnnouncement::SetDefeatedAnnouncementText(FString AttackerName, FString VictimName)
{
	FString DefeatedAnnouncementText = FString::Printf(TEXT("%s 击败 %s!"), *AttackerName, *VictimName);
	if (AnnouncementText)
	{
		AnnouncementText->SetText(FText::FromString(DefeatedAnnouncementText));
	}

}
