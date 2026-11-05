// Fill out your copyright notice in the Description page of Project Settings.


#include "BrokenEdgeHUD.h"

#include "CharacterOverlay.h"
#include "DefeatedAnnouncement.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Engine/Canvas.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "BrokenEdge/Weapon/WeaponPresentation.h"
#include "Engine/Texture2D.h"
#include "Camera/PlayerCameraManager.h"
#include "BrokenEdge/Extraction/ExtractionCombatComponent.h"


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
		if (auto* Character = Cast<ABrokenEdgeCharacter>(GetOwningPawn());
			Character && Character->IsFirstPersonScopeActive())
		{
			const auto* Presentation = Character->GetEquippedWeapon()->Presentation.Get();
			auto* Texture = Presentation->ScopeOverlay.Get();
			if (Texture && Texture->GetResource())
			{
				const float Height = Canvas->SizeY;
				const float Width = Height * Texture->GetSizeX() / FMath::Max(1, Texture->GetSizeY());
				DrawTexture(Texture, ViewportCenter.X - Width * .5f, 0, Width, Height,
				            0, 0, 1, 1, FLinearColor::White);
				return;
			}
		}

		float SpreadScaled = CrosshairSpreadMax * HUDPackage.CrosshairSpread;
		if (HUDPackage.bProceduralCrosshair)
		{
			const float Scale = Canvas->SizeY / 1080.f;
			const auto* PC = GetOwningPlayerController();
			const float FOV = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : 90.f;
			const float Focal = Canvas->SizeX / (2.f * FMath::Tan(FMath::DegreesToRadians(FOV * .5f)));
			const float Baseline = (HUDPackage.CrosshairSpread - HUDPackage.ShotPulse) * CrosshairSpreadMax * Scale;
			const float Pulse = HUDPackage.ShotPulse * 16.f * Scale;
			const float X = FMath::Clamp(FMath::Max(Baseline,
			                                        Focal * FMath::Tan(
				                                        FMath::DegreesToRadians(HUDPackage.AngularSpread.X))) + Pulse,
			                             4.f * Scale, 80.f * Scale);
			const float Y = FMath::Clamp(FMath::Max(Baseline,
			                                        Focal * FMath::Tan(
				                                        FMath::DegreesToRadians(HUDPackage.AngularSpread.Y))) + Pulse,
			                             4.f * Scale, 80.f * Scale);
			const float Length = 7.f * Scale;
			const auto Color = HUDPackage.CrosshairsColor;
			for (float Sign : {-1.f, 1.f})
			{
				DrawLine(ViewportCenter.X + Sign * X, ViewportCenter.Y,
				         ViewportCenter.X + Sign * (X + Length), ViewportCenter.Y, Color, 1.5f * Scale);
				DrawLine(ViewportCenter.X, ViewportCenter.Y + Sign * Y,
				         ViewportCenter.X, ViewportCenter.Y + Sign * (Y + Length), Color, 1.5f * Scale);
			}
			return;
		}

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
