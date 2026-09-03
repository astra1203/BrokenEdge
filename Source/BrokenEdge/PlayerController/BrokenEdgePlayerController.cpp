// Fill out your copyright notice in the Description page of Project Settings.


#include "BrokenEdgePlayerController.h"

#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/GameMode/BrokenEdgeGameMode.h"
#include "BrokenEdge/UI/BrokenEdgeHUD.h"
#include "BrokenEdge/UI/CharacterOverlay.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"


void ABrokenEdgePlayerController::BeginPlay()
{
	Super::BeginPlay();
	BrokenEdgeHUD = Cast<ABrokenEdgeHUD>(GetHUD());
}

void ABrokenEdgePlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABrokenEdgePlayerController, bShowTeamScores);
}

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

float ABrokenEdgePlayerController::GetServerTime()
{
	if (HasAuthority())return GetWorld()->GetTimeSeconds();
	return GetWorld()->GetTimeSeconds()+ClientServerDelta;
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
	CheckTimeSync(DeltaTime);
	//@TODO
	
}

void ABrokenEdgePlayerController::CheckTimeSync(float DeltaTime)
{
	TimeSyncRunningTime+=DeltaTime;
	if (IsLocalController() && TimeSyncRunningTime > TimeSyncFrequency)
	{
		TimeSyncRunningTime = 0.f;
		ServerRequestServerTime(GetWorld()->GetTimeSeconds());
		
	}
	
	
}

void ABrokenEdgePlayerController::ServerRequestServerTime_Implementation(float TimeOfClientRequest)
{
	float ServerTimeOfReceipt = GetWorld()->GetTimeSeconds();
	ClientReportServerTime(TimeOfClientRequest, ServerTimeOfReceipt);
}

void ABrokenEdgePlayerController::ClientReportServerTime_Implementation(float TimeOfClientRequest,
	float TimeServerReceivedClientRequest)
{
	float RoundTripTime = GetWorld()->GetTimeSeconds() - TimeOfClientRequest;
	SingleTripTime = RoundTripTime * 0.5f;
	float CurrentServerTime = TimeServerReceivedClientRequest + SingleTripTime;
	ClientServerDelta = CurrentServerTime- GetWorld()->GetTimeSeconds();
	
	if (BrokenEdgeHUD&&BrokenEdgeHUD->CharacterOverlay&&BrokenEdgeHUD->CharacterOverlay->LagText)
	{
		BrokenEdgeHUD->CharacterOverlay->LagText->SetText(FText::FromString(FString::Printf(TEXT("延迟：%.2f"), SingleTripTime)));
	}
	
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
				if (bInitializeHealth) SetHUDHealth(HUDHealth, HUDMaxHealth);
				if (bInitializeShield) SetHUDShield(HUDShield, HUDMaxShield);
				if (bInitializeWeaponAmmo) SetHUDWeaponAmmo(HUDWeaponAmmo);
				if (bInitializeCarriedAmmo) SetHUDCarriedAmmo(HUDCarriedAmmo);
				
				if (bInitializeScore) SetHUDScore(HUDScore);
				if (bInitializeDefeats) SetHUDDefeats(HUDDefeats);
				//@TODO
				// ABlasterCharacter* BlasterCharacter = Cast<ABlasterCharacter>(GetPawn());
				// if (BlasterCharacter && BlasterCharacter->GetCombat())
				// {
				// 	if (bInitializeGrenades) SetHUDGrenades(BlasterCharacter->GetCombat()->GetGrenades());
				// }

				if (bShowTeamScores)
				{
					InitTeamScores();
				}
				else
				{
					// HideTeamScores();
				}
				
			}
			
		}
		
	}
	
}

void ABrokenEdgePlayerController::ClientJoinMidgame_Implementation(FName StateOfMatch, float Warmup, float Match,
	float Cooldown, float StartingTime, bool bIsTeamsMatch)
{
}

void ABrokenEdgePlayerController::OnRep_MatchState()
{
}

void ABrokenEdgePlayerController::ServerCheckMatchState_Implementation()
{
	ABrokenEdgeGameMode* GameMode = Cast<ABrokenEdgeGameMode>(UGameplayStatics::GetGameMode(this));
	if (GameMode==nullptr)return;

	WarmupTime = GameMode->WarmupTime;
	MatchTime = GameMode->MatchTime;
	CooldownTime = GameMode->CooldownTime;
	LevelStartingTime = GameMode->LevelStartingTime;
	MatchState = GameMode->GetMatchState();
	bShowTeamScores = GameMode->bTeamsMatch;

	// ClientJoinMidgame();

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

void ABrokenEdgePlayerController::SetHUDScore(float Score)
{
	BrokenEdgeHUD = BrokenEdgeHUD == nullptr ? Cast<ABrokenEdgeHUD>(GetHUD()) : BrokenEdgeHUD;
	
	bool bHUDValid = BrokenEdgeHUD &&
		BrokenEdgeHUD->CharacterOverlay &&
		BrokenEdgeHUD->CharacterOverlay->ScoreAmount;
	
	if (bHUDValid)
	{
		FString ScoreText = FString::Printf(TEXT("%d"), FMath::FloorToInt(Score));
		BrokenEdgeHUD->CharacterOverlay->ScoreAmount->SetText(FText::FromString(ScoreText));
	}
	else
	{
		bInitializeScore = true;
		HUDScore = Score;
	}
}

void ABrokenEdgePlayerController::SetHUDDefeats(int32 Defeats)
{
	BrokenEdgeHUD = BrokenEdgeHUD == nullptr ? Cast<ABrokenEdgeHUD>(GetHUD()) : BrokenEdgeHUD;
	
	bool bHUDValid = BrokenEdgeHUD &&
		BrokenEdgeHUD->CharacterOverlay &&
		BrokenEdgeHUD->CharacterOverlay->DefeatsAmount;
	
	if (bHUDValid)
	{
		FString DefeatsText = FString::Printf(TEXT("%d"), Defeats);
		BrokenEdgeHUD->CharacterOverlay->DefeatsAmount->SetText(FText::FromString(DefeatsText));
	}
	else
	{
		bInitializeDefeats = true;
		HUDDefeats = Defeats;
	}
}

/**
 * @brief 设置蓝队分数
 * @param BlueScore 蓝队分数
 */
void ABrokenEdgePlayerController::SetHUDBlueTeamScore(int32 BlueScore)
{
	BrokenEdgeHUD = BrokenEdgeHUD == nullptr ? Cast<ABrokenEdgeHUD>(GetHUD()) : BrokenEdgeHUD;
	
	bool bHUDValid = BrokenEdgeHUD &&
		BrokenEdgeHUD->CharacterOverlay &&
		BrokenEdgeHUD->CharacterOverlay->BlueTeamScore;
	
	if (bHUDValid)
	{
		FString ScoreText = FString::Printf(TEXT("%d"), BlueScore);
		BrokenEdgeHUD->CharacterOverlay->BlueTeamScore->SetText(FText::FromString(ScoreText));
	}
}

/**
 * @brief 设置红队分数
 * @param RedScore 红队分数
 */
void ABrokenEdgePlayerController::SetHUDRedTeamScore(int32 RedScore)
{
	BrokenEdgeHUD = BrokenEdgeHUD == nullptr ? Cast<ABrokenEdgeHUD>(GetHUD()) : BrokenEdgeHUD;
	
	bool bHUDValid = BrokenEdgeHUD &&
		BrokenEdgeHUD->CharacterOverlay &&
		BrokenEdgeHUD->CharacterOverlay->RedTeamScore;
	
	if (bHUDValid)
	{
		FString ScoreText = FString::Printf(TEXT("%d"), RedScore);
		BrokenEdgeHUD->CharacterOverlay->RedTeamScore->SetText(FText::FromString(ScoreText));
	}
}

void ABrokenEdgePlayerController::InitTeamScores()
{
	BrokenEdgeHUD = BrokenEdgeHUD == nullptr ? Cast<ABrokenEdgeHUD>(GetHUD()) : BrokenEdgeHUD;

	bool bHUDValid = BrokenEdgeHUD &&
		BrokenEdgeHUD->CharacterOverlay &&
		BrokenEdgeHUD->CharacterOverlay->RedTeamScore &&
		BrokenEdgeHUD->CharacterOverlay->BlueTeamScore &&
		BrokenEdgeHUD->CharacterOverlay->ScoreSpacerText;

	if (bHUDValid)
	{
		FString Zero("0");
		FString Spacer("|");
		BrokenEdgeHUD->CharacterOverlay->RedTeamScore->SetText(FText::FromString(Zero));
		BrokenEdgeHUD->CharacterOverlay->BlueTeamScore->SetText(FText::FromString(Zero));
		BrokenEdgeHUD->CharacterOverlay->ScoreSpacerText->SetText(FText::FromString(Spacer));
	}
}

void ABrokenEdgePlayerController::HideTeamScores()
{
	BrokenEdgeHUD = BrokenEdgeHUD == nullptr ? Cast<ABrokenEdgeHUD>(GetHUD()) : BrokenEdgeHUD;

	bool bHUDValid = BrokenEdgeHUD &&
		BrokenEdgeHUD->CharacterOverlay &&
		BrokenEdgeHUD->CharacterOverlay->RedTeamScore &&
		BrokenEdgeHUD->CharacterOverlay->BlueTeamScore &&
		BrokenEdgeHUD->CharacterOverlay->ScoreSpacerText;

	if (bHUDValid)
	{
		BrokenEdgeHUD->CharacterOverlay->RedTeamScore->SetText(FText());
		BrokenEdgeHUD->CharacterOverlay->BlueTeamScore->SetText(FText());
		BrokenEdgeHUD->CharacterOverlay->ScoreSpacerText->SetText(FText());
	}
}
