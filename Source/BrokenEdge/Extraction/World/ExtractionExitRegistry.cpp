#include "BrokenEdge/Extraction/World/ExtractionExitRegistry.h"
#include "BrokenEdge/Extraction/World/ExtractionExit.h"
void UExtractionExitRegistry::Register(AExtractionExit* Exit)
{
    if (IsValid(Exit) && Exit->GetWorld() == GetWorld()) Exits.AddUnique(Exit);
}
void UExtractionExitRegistry::Unregister(AExtractionExit* Exit)
{
    Exits.RemoveAll([Exit](const auto& Entry) { return !Entry.IsValid() || Entry.Get() == Exit; });
}
