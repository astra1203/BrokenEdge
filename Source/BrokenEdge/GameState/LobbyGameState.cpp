#include "LobbyGameState.h"
#include "Net/UnrealNetwork.h"

void ALobbyGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ALobbyGameState, RoomMatchType);
    DOREPLIFETIME(ALobbyGameState, ExtractionMapId);
    DOREPLIFETIME(ALobbyGameState, bModeUpdating);
    DOREPLIFETIME(ALobbyGameState, MinimumPlayers);
    DOREPLIFETIME(ALobbyGameState, Capacity);
    DOREPLIFETIME(ALobbyGameState, CountdownEndTime);
    DOREPLIFETIME(ALobbyGameState, bTravelling);
    DOREPLIFETIME(ALobbyGameState, LobbyStatus);
}
