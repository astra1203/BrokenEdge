// Copyright Epic Games, Inc. All Rights Reserved.

#include "BrokenEdge.h"
#include "Engine/World.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE( FDefaultGameModuleImpl, BrokenEdge, "BrokenEdge" );

FString PrintNetMode()
{
	if (GWorld == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NetMode] World=null | Mode=Unavailable"));
		return FString("Unavailable");
	}

	const TCHAR* NetModeText = TEXT("Unknown");
	switch (GWorld->GetNetMode())
	{
	case NM_Standalone:
		NetModeText = TEXT("Standalone");
		break;
	case NM_DedicatedServer:
		NetModeText = TEXT("DedicatedServer");
		break;
	case NM_ListenServer:
		NetModeText = TEXT("ListenServer");
		break;
	case NM_Client:
		NetModeText = TEXT("Client");
		break;
	default:
		break;
	}

	return NetModeText;
	// UE_LOG(LogTemp, Warning, TEXT("[NetMode] World=%s | Mode=%s"), *GetNameSafe(GWorld), NetModeText);
}
