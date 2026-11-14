// Fill out your copyright notice in the Description page of Project Settings.


#include "CombatObserver.h"

#include "MyCharacter.h"


void UCombatObserver::StartObserving(AMyCharacter* Character)
{
	StopObserving();

	if (!IsValid(Character))
	{
		return;
	}

	ObservedCharacter = Character;
	
	
	HealthChangedHandle = Character->OnHealthChanged.AddUObject(
		this,
		&UCombatObserver::HandleHealthChanged);
	
	// const FDelegateHandle AudioHandle = Character->OnHealthChanged.AddLambda(
	// [](const float NewHealth, const float HealthDelta)
	// {
	// 	if (HealthDelta < 0.0f)
	// 	{
	// 		UE_LOG(LogTemp, Log, TEXT("Play hurt sound"));
	// 	}
	// });
	
	
}

void UCombatObserver::StopObserving()
{
	if (AMyCharacter* HealthComponent = ObservedCharacter.Get())
	{
		HealthComponent->OnHealthChanged.Remove(HealthChangedHandle);
	}

	HealthChangedHandle.Reset();
	ObservedCharacter.Reset();

}

void UCombatObserver::HandleHealthChanged(float NewHealth, float HealthDelta)
{
	UE_LOG(
	LogTemp,
	Log,
	TEXT("Health changed: NewHealth=%.1f Delta=%.1f"),
	NewHealth,
	HealthDelta);
}
