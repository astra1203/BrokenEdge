// Fill out your copyright notice in the Description page of Project Settings.


#include "BrokenEdgePlayerController.h"

#include "BrokenEdge/UI/BrokenEdgeHUD.h"
#include "BrokenEdge/UI/CharacterOverlay.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"


void ABrokenEdgePlayerController::SetHUDHealth(float Health, float MaxHealth)
{
	BrokenEdgeHUD = BrokenEdgeHUD == nullptr ? Cast<ABrokenEdgeHUD>(GetHUD()) : BrokenEdgeHUD;
	
	bool bHUDValid = BrokenEdgeHUD &&
		BrokenEdgeHUD->CharacterOverlay &&
		BrokenEdgeHUD->CharacterOverlay->HealthBar &&
		BrokenEdgeHUD->CharacterOverlay->HealthText;
	
	if (bHUDValid)
	{
		const float HealthPercent = Health / MaxHealth;
		BrokenEdgeHUD->CharacterOverlay->HealthBar->SetPercent(HealthPercent);
		
		FString HealthText = FString::Printf(TEXT("%d/%d"), FMath::CeilToInt(Health), FMath::CeilToInt(MaxHealth));
		BrokenEdgeHUD->CharacterOverlay->HealthText->SetText(FText::FromString(HealthText));
	}
	else
	{
		bInitializeHealth = true;
		HUDHealth = Health;
		HUDMaxHealth = MaxHealth;
	}
}
