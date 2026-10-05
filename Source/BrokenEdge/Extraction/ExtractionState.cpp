#include "ExtractionState.h"
#include "ExtractionInventoryComponent.h"
#include "Net/UnrealNetwork.h"

AExtractionPlayerState::AExtractionPlayerState()
{
	Inventory = CreateDefaultSubobject<UExtractionInventoryComponent>(TEXT("ExtractionInventory"));
}

void AExtractionGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionGameState, Phase);
	DOREPLIFETIME(AExtractionGameState, Deadline);
	DOREPLIFETIME(AExtractionGameState, AlivePlayers);
	DOREPLIFETIME(AExtractionGameState, EndReason);
	DOREPLIFETIME(AExtractionGameState, StorageError);
}

void AExtractionPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionPlayerState, Life);
	DOREPLIFETIME(AExtractionPlayerState, BleedAt);
	DOREPLIFETIME(AExtractionPlayerState, DownHealth);
	DOREPLIFETIME_CONDITION(AExtractionPlayerState, ExtractAt, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AExtractionPlayerState, RaidProgress, COND_OwnerOnly);
}
