// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "BrokenEdgeGameMode.generated.h"

/**
 *
 */
UCLASS()
class BROKENEDGE_API ABrokenEdgeGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	ABrokenEdgeGameMode();
	
	// UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Classes)
	// UClass* cla;
	
	virtual void BeginPlay() override;
};
