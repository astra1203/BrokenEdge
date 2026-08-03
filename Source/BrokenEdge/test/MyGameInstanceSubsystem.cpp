// Fill out your copyright notice in the Description page of Project Settings.


#include "MyGameInstanceSubsystem.h"


UMyGameInstanceSubsystem::UMyGameInstanceSubsystem()
{
	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT(""));
}

void UMyGameInstanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT(""));
}

void UMyGameInstanceSubsystem::Deinitialize()
{
	Super::Deinitialize();
	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT(""));\
}

void UMyGameInstanceSubsystem::BeginDestroy()
{
	Super::BeginDestroy();
	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT(""));
}

UMyGameInstanceSubsystem::~UMyGameInstanceSubsystem()
{
	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT(""));
}
