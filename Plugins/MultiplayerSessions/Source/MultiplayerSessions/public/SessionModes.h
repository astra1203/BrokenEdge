#pragma once
#include "CoreMinimal.h"

/** Stable advertised mode identifiers shared by the browser, lobby and tests. */
namespace SessionModes
{
	inline FString Map(const FString& Mode)
	{
		if (Mode == TEXT("FreeForAll")) return TEXT("/Game/Maps/FreeForAll");
		if (Mode == TEXT("Teams")) return TEXT("/Game/Maps/Teams");
		if (Mode == TEXT("Extraction")) return TEXT("/Game/Maps/SpaceCenter/L_SpaceCenterExtraction");
		return {};
	}

	inline bool IsSupported(const FString& Mode) { return !Map(Mode).IsEmpty(); }
}
