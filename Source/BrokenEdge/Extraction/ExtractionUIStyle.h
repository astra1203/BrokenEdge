#pragma once
#include "ExtractionItems.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "UObject/StrongObjectPtr.h"
// 背包与仓库共用的纯展示工具：统一设计坐标、品质色和物品图标绘制，不执行物品操作。

/** Shared presentation for stash and raid grids; no inventory mutations. */
namespace ExtractionUIStyle
{
	// 以 1280×720 为设计尺寸等比缩放，设置最小比例避免除零及极小窗口退化。
	inline float Scale(const FGeometry& G) { return FMath::Max(.1f,FMath::Min(G.GetLocalSize().X/1280,G.GetLocalSize().Y/720)); }
// 将缩放后的设计画布居中；输入命中测试也必须撤销这份偏移。
inline FVector2D Offset(const FGeometry& G) { return (G.GetLocalSize()-FVector2D(1280,720)*Scale(G))*.5f; }

// 把品质等级限制到颜色表有效范围，避免异常数据越界。
inline FLinearColor Rarity(int32 Level)
	{
		const FLinearColor Colors[] = {
			{.36f, .43f, .43f}, {.25f, .65f, .43f}, {.2f, .48f, .8f},
			{.58f, .32f, .75f}, {.8f, .59f, .23f}, {.85f, .28f, .24f}
		};
		return Colors[FMath::Clamp(Level, 0, 5)];
	}

	// 同步加载并缓存软引用纹理，以强引用防止 GC 回收；按宽高比居中绘制，失败返回 false 供调用者显示文本。
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
