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
}

void AExtractionPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionPlayerState, Life);
}
