#pragma once
#include "ExtractionItems.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "UObject/StrongObjectPtr.h"

/** Shared presentation for stash and raid grids; no inventory mutations. */
namespace ExtractionUIStyle
{
	inline float Scale(const FGeometry& G) { return FMath::Max(.1f,FMath::Min(G.GetLocalSize().X/1280,G.GetLocalSize().Y/720)); }
inline FVector2D Offset(const FGeometry& G) { return (G.GetLocalSize()-FVector2D(1280,720)*Scale(G))*.5f; }

inline FLinearColor Rarity(int32 Level)
	{
		const FLinearColor Colors[] = {
			{.36f, .43f, .43f}, {.25f, .65f, .43f}, {.2f, .48f, .8f},
			{.58f, .32f, .75f}, {.8f, .59f, .23f}, {.85f, .28f, .24f}
		};
		return Colors[FMath::Clamp(Level, 0, 5)];
	}

	inline bool Icon(FSlateWindowElementList& Draw, int32 Layer, const FGeometry& G,
	                 FVector2D At, FVector2D Size, float Scale, const UExtractionItemDefinition* Definition)
	{
		if (!Definition || Definition->Icon.IsNull()) return false;
		// Keep texture references alive across GC and share the small cache between both grids.
		static TMap<FSoftObjectPath, TStrongObjectPtr<UTexture2D>> Textures;
		const auto Path = Definition->Icon.ToSoftObjectPath();
		if (!Textures.Contains(Path)) Textures.Add(
			Path, TStrongObjectPtr<UTexture2D>(Definition->Icon.LoadSynchronous()));
		UTexture2D* Texture = Textures[Path].Get();
		if (!Texture) return false;
		const float Fit = FMath::Min(Size.X / FMath::Max(1, Texture->GetSizeX()),
		                             Size.Y / FMath::Max(1, Texture->GetSizeY()));
		const FVector2D ImageSize(Texture->GetSizeX() * Fit, Texture->GetSizeY() * Fit);
		FSlateBrush Brush;
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = ImageSize;
		FSlateDrawElement::MakeBox(Draw, Layer, G.ToPaintGeometry(ImageSize * Scale,
		                                                          FSlateLayoutTransform(
			                                                          Offset(G) + (At + (Size - ImageSize) * .5f) * Scale)), &Brush,
		                           ESlateDrawEffect::None, FLinearColor::White);
		return true;
	}
}
