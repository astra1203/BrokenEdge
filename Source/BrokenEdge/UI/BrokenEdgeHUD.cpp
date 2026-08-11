// Fill out your copyright notice in the Description page of Project Settings.


#include "BrokenEdgeHUD.h"

#include "CharacterOverlay.h"
#include "Blueprint/UserWidget.h"


void ABrokenEdgeHUD::BeginPlay()
{
	Super::BeginPlay();
	//@TODO
	AddCharacterOverlay();
}

void ABrokenEdgeHUD::AddCharacterOverlay()
{
	TObjectPtr<APlayerController> PlayerController = GetOwningPlayerController();
	if (PlayerController && CharacterOverlayClass)
	{
		CharacterOverlay = CreateWidget<UCharacterOverlay>(PlayerController, CharacterOverlayClass);
		CharacterOverlay->AddToViewport();
	}

}
