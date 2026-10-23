#include "WeaponRangeGameMode.h"
#include "ExtractionState.h"
#include "ExtractionInventoryComponent.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"

AActor* AWeaponRangeGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// Local training has no lobby squad assignment; use its explicit PlayerStart.
	return AGameModeBase::ChoosePlayerStart_Implementation(Player);
}

void AWeaponRangeGameMode::InitGame(const FString& Map, const FString& Options, FString& Error)
{
	MapDefinition = FSoftObjectPath(TEXT("/Game/Extraction/Training/DA_WeaponRangeMap.DA_WeaponRangeMap"));
	Super::InitGame(Map, Options + TEXT("?ExpectedPlayers=1"), Error);
}

void AWeaponRangeGameMode::HandleStartingNewPlayer_Implementation(APlayerController* Player)
{
	Super::HandleStartingNewPlayer_Implementation(Player);
	auto* PS = Player ? Player->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!PS || !Player->GetPawn()) return;
	auto* Inventory = PS->Inventory.Get();
	Inventory->Contents = FExtractionContainer();
	Inventory->Contents.Id = FGuid::NewGuid();
	Inventory->Contents.WeaponSlots = true;
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		auto Gun = ExtractionItems::Make(Slot == 0 ? TEXT("AK") : TEXT("SCARH"));
		Gun.Slot = Slot;
		Inventory->Contents.Items.Add(Gun);
	}
	for (FName Id : {
		     FName(TEXT("Ammo")), FName(TEXT("Ammo76251")), FName(TEXT("VerticalGrip")), FName(TEXT("RedDot")),
		     FName(TEXT("Compensator"))
	     })
		ExtractionItems::AutoPlace(Inventory->Contents,
		                           ExtractionItems::Make(Id, Id.ToString().StartsWith(TEXT("Ammo")) ? 60 : 1));
	Inventory->SyncWeapons();
	Inventory->Changed();
}

void AWeaponRangeGameMode::Tick(float Delta)
{
	// Training deliberately does not tick raid start/settlement or issue persistent rewards.
	auto* GS = GetGameState<AExtractionGameState>();
	if (!GS) return;
	GS->Phase = EExtractionPhase::Active;
	GS->Deadline = GetWorld()->GetTimeSeconds() + 3600;
	GS->StorageError.Reset();
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		if (auto* C = Cast<ABrokenEdgeCharacter>(It->Get()->GetPawn()))
		{
			C->bDisableGameplay = false;
			if (C->GetCharacterMovement()->MovementMode == MOVE_None)
				C->GetCharacterMovement()->SetMovementMode(
					MOVE_Walking);
		}
}
