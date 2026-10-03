// Fill out your copyright notice in the Description page of Project Settings.


#include "BrokenEdgeHUD.h"

#include "CharacterOverlay.h"
#include "DefeatedAnnouncement.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Engine/Canvas.h"


void ABrokenEdgeHUD::BeginPlay()
{
	Super::BeginPlay();
	//@TODO
	AddCharacterOverlay();
}

void ABrokenEdgeHUD::DrawHUD()
{
	Super::DrawHUD();
	if (Canvas && IsValid(GetOwningPawn()))
	{
		// Draw in this player's canvas, including resized PIE and high-resolution captures.
		const FVector2D ViewportCenter(Canvas->SizeX / 2.f, Canvas->SizeY / 2.f);

		float SpreadScaled = CrosshairSpreadMax * HUDPackage.CrosshairSpread;

		if (HUDPackage.CrosshairsCenter)
		{
			FVector2D Spread(0.f, 0.f);
			DrawCrosshair(HUDPackage.CrosshairsCenter, ViewportCenter, Spread, HUDPackage.CrosshairsColor);
		}
		if (HUDPackage.CrosshairsLeft)
		{
			FVector2D Spread(-SpreadScaled, 0.f);
			DrawCrosshair(HUDPackage.CrosshairsLeft, ViewportCenter, Spread, HUDPackage.CrosshairsColor);
		}
		if (HUDPackage.CrosshairsRight)
		{
			FVector2D Spread(SpreadScaled, 0.f);
			DrawCrosshair(HUDPackage.CrosshairsRight, ViewportCenter, Spread, HUDPackage.CrosshairsColor);
		}
		if (HUDPackage.CrosshairsTop)
		{
			FVector2D Spread(0.f, -SpreadScaled);
			DrawCrosshair(HUDPackage.CrosshairsTop, ViewportCenter, Spread, HUDPackage.CrosshairsColor);
		}
		if (HUDPackage.CrosshairsBottom)
		{
			FVector2D Spread(0.f, SpreadScaled);
			DrawCrosshair(HUDPackage.CrosshairsBottom, ViewportCenter, Spread, HUDPackage.CrosshairsColor);
		}
	}
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

void ABrokenEdgeHUD::AddDefeatedAnnouncement(FString AttackerName, FString VictimName)
{
	TObjectPtr<APlayerController> PlayerController = GetOwningPlayerController();
	if (PlayerController == nullptr || DefeatedAnnouncementClass == nullptr) return;

	UDefeatedAnnouncement* DefeatedAnnouncement = CreateWidget<UDefeatedAnnouncement>(
		PlayerController, DefeatedAnnouncementClass);
	if (DefeatedAnnouncement == nullptr) return;

	DefeatedAnnouncement->SetDefeatedAnnouncementText(AttackerName, VictimName);
	DefeatedAnnouncement->AddToViewport();

	for (UDefeatedAnnouncement* DefeatedMessage : DefeatedMessages)
	{
		if (DefeatedMessage && DefeatedMessage->AnnouncementBox)
		{
			UCanvasPanelSlot* CanvasSlot = UWidgetLayoutLibrary::SlotAsCanvasSlot(DefeatedMessage->AnnouncementBox);

			if (CanvasSlot)
			{
				FVector2D Position = CanvasSlot->GetPosition();

				FVector2D NewPosition(
					Position.X,
					Position.Y - CanvasSlot->GetSize().Y
				);

				UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s  %f, %f"), GFrameCounter, TEXT(__FUNCTION__),
				       __LINE__,
				       TEXT("NewPosition:"), NewPosition.X, NewPosition.Y);

				CanvasSlot->SetPosition(NewPosition);
			}
		}
	}

	DefeatedMessages.Add(DefeatedAnnouncement);

	FTimerHandle DefeatedMsgTimerHandle;

	FTimerDelegate DefeatedMsgDelegate;
	DefeatedMsgDelegate.BindUFunction(this, FName("DefeatedAnnouncementTimerFinished"), DefeatedAnnouncement);

	GetWorldTimerManager().SetTimer(
		DefeatedMsgTimerHandle,
		DefeatedMsgDelegate,
		DefeatedAnnouncementTime,
		false);
}

void ABrokenEdgeHUD::DrawCrosshair(UTexture2D* Texture, FVector2D ViewportCenter, FVector2D Spread,
                                   FLinearColor CrosshairColor)
{
	if (!IsValid(Texture) || !Texture->GetResource()) return;
	const float TextureWidth = Texture->GetSizeX();
	const float TextureHeight = Texture->GetSizeY();

	const FVector2D TextureDrawPoint(ViewportCenter.X - (TextureWidth / 2.f) + Spread.X,
	                                 ViewportCenter.Y - (TextureHeight / 2.f) + Spread.Y);

	DrawTexture(Texture, TextureDrawPoint.X, TextureDrawPoint.Y, TextureWidth,
	            TextureHeight, 0.f, 0.f, 1.f, 1.f, CrosshairColor);
}

void ABrokenEdgeHUD::DefeatedAnnouncementTimerFinished(UDefeatedAnnouncement* MsgToRemove)
{
	if (MsgToRemove)
	{
		MsgToRemove->RemoveFromParent();
	}
}
