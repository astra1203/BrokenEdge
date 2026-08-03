// Copyright Epic Games, Inc. All Rights Reserved.

#include "BrokenEdge.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE( FDefaultGameModuleImpl, BrokenEdge, "BrokenEdge" );

void func()
{
	UE_LOG(LogTemp, Warning, TEXT("Hello World!"));
}
