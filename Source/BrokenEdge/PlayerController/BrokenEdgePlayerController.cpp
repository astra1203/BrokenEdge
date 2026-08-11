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

void ABrokenEdgePlayerController::SetHUDShield(float Shield, float MaxShield)
{
	BrokenEdgeHUD = BrokenEdgeHUD == nullptr ? Cast<ABrokenEdgeHUD>(GetHUD()) : BrokenEdgeHUD;
	bool bHUDValid = BrokenEdgeHUD &&
		BrokenEdgeHUD->CharacterOverlay &&
		BrokenEdgeHUD->CharacterOverlay->ShieldBar &&
		BrokenEdgeHUD->CharacterOverlay->ShieldText;
	if (bHUDValid)
	{
		const float ShieldPercent = Shield / MaxShield;
		BrokenEdgeHUD->CharacterOverlay->ShieldBar->SetPercent(ShieldPercent);
		FString ShieldText = FString::Printf(TEXT("%d/%d"), FMath::CeilToInt(Shield), FMath::CeilToInt(MaxShield));
		BrokenEdgeHUD->CharacterOverlay->ShieldText->SetText(FText::FromString(ShieldText));
	}
	else
	{
		bInitializeShield = true;
		HUDShield = Shield;
		HUDMaxShield = MaxShield;
	}
}
