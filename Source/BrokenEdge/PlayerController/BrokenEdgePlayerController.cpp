// Fill out your copyright notice in the Description page of Project Settings.


#include "BrokenEdgePlayerController.h"

#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
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

void ABrokenEdgePlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (TObjectPtr<ABrokenEdgeCharacter> BrokenEdgeCharacter = Cast<ABrokenEdgeCharacter>(InPawn)) 
	{
		SetHUDHealth(BrokenEdgeCharacter->GetHealth(), BrokenEdgeCharacter->GetMaxHealth());
		SetHUDShield(BrokenEdgeCharacter->GetShield(), BrokenEdgeCharacter->GetMaxShield());
		// SetHUDWeaponAmmo(BrokenEdgeCharacter->Combat->WeaponAmmoAmount);
	}
	
}

void ABrokenEdgePlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	PollInit();
}

void ABrokenEdgePlayerController::PollInit()
{
	if (CharacterOverlay==nullptr)
	{
		if (BrokenEdgeHUD&&BrokenEdgeHUD->CharacterOverlay)
		{
			CharacterOverlay = BrokenEdgeHUD->CharacterOverlay;
			
			if (CharacterOverlay)
			{
				// if (!HasAuthority())
				// {
				// 	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | LocalCharacter: %s"), GFrameCounter, TEXT(__FUNCTION__),
				// 	       __LINE__, *GetNameSafe(GetCharacter()));
				// }
				
				if (bInitializeHealth) SetHUDHealth(HUDHealth, HUDMaxHealth);
				if (bInitializeShield) SetHUDShield(HUDShield, HUDMaxShield);
				if (bInitializeWeaponAmmo) SetHUDWeaponAmmo(HUDWeaponAmmo);
				if (bInitializeCarriedAmmo) SetHUDCarriedAmmo(HUDCarriedAmmo);
				//@TODO
				
			}
			
		}
		
	}
	
}

void ABrokenEdgePlayerController::SetHUDWeaponAmmo(int32 Ammo)
{
	BrokenEdgeHUD = BrokenEdgeHUD == nullptr ? Cast<ABrokenEdgeHUD>(GetHUD()) : BrokenEdgeHUD;
	
	bool bHUDValid = BrokenEdgeHUD &&
		BrokenEdgeHUD->CharacterOverlay &&
		BrokenEdgeHUD->CharacterOverlay->WeaponAmmoAmount;
	
	if (bHUDValid)
	{
		FString AmmoText = FString::Printf(TEXT("%d"), Ammo);
		BrokenEdgeHUD->CharacterOverlay->WeaponAmmoAmount->SetText(FText::FromString(AmmoText));
	}
	else
	{
		bInitializeWeaponAmmo = true;
		HUDWeaponAmmo = Ammo;
	}
}

void ABrokenEdgePlayerController::SetHUDCarriedAmmo(int32 Ammo)
{
	BrokenEdgeHUD = BrokenEdgeHUD == nullptr ? Cast<ABrokenEdgeHUD>(GetHUD()) : BrokenEdgeHUD;
	
	bool bHUDValid = BrokenEdgeHUD &&
		BrokenEdgeHUD->CharacterOverlay &&
		BrokenEdgeHUD->CharacterOverlay->CarriedAmmoAmount;
	
	if (bHUDValid)
	{
		FString AmmoText = FString::Printf(TEXT("%d"), Ammo);
		BrokenEdgeHUD->CharacterOverlay->CarriedAmmoAmount->SetText(FText::FromString(AmmoText));
	}
	else
	{
		bInitializeCarriedAmmo = true;
		HUDCarriedAmmo = Ammo;
	}
}
