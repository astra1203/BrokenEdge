#include "ExtractionBlueprintTestMode.h"
#include "GameFramework/DefaultPawn.h"

AExtractionBlueprintTestMode::AExtractionBlueprintTestMode()
{
	DefaultPawnClass = ADefaultPawn::StaticClass();
	bDelayedStart = false;
}
