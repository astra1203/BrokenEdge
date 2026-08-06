// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "CombatObserver.generated.h"

class AMyCharacter;

/**
 * 
 */
UCLASS()
class BROKENEDGE_API UCombatObserver : public UObject
{
	GENERATED_BODY()
public:
	void StartObserving(AMyCharacter* Character);
	void StopObserving();

private:
	void HandleHealthChanged(float NewHealth, float HealthDelta);

	TWeakObjectPtr<AMyCharacter> ObservedCharacter;
	
	FDelegateHandle HealthChangedHandle;
	
	
	
};
