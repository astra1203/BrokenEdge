#pragma once
#if WITH_DEV_AUTOMATION_TESTS
#include "BrokenEdge/Extraction/Inventory/ExtractionInventoryComponent.h"

// Test fixtures can construct states without exposing a mutable production accessor.
struct FExtractionInventoryTestAccess
{
    static FExtractionContainer& Get(UExtractionInventoryComponent* Inventory) { return Inventory->Contents; }
};
#endif
