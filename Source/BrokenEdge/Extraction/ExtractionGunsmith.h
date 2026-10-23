#pragma once
#include "CoreMinimal.h"
#include "ExtractionItems.h"

class SWidget;
namespace ExtractionGunsmith
{
	// Snapshot reads and production transaction callbacks are supplied by inventory/stash hosts.
	void Open(TSharedRef<SWidget> Parent, FGuid Weapon, TFunction<FExtractionContainer()> Read,
	          TFunction<void(const FExtractionItemRequest&)> Submit);
}
