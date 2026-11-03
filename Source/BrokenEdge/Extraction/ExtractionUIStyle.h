#pragma once
#include "ExtractionItems.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/Font.h"
#include "Styling/CoreStyle.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
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
		// 官方 DFMColorDefine.ItemQuality_1..6（白/绿/蓝/紫/橙/红）。
		static const FLinearColor Colors[] = {
			FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("989898"))),
			FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("08A173"))),
			FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("3E80B3"))),
			FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("7F59B3"))),
			FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("AA6433"))),
			FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("B33E3E")))
		};
		return Colors[FMath::Clamp(Level, 0, 5)];
	}

	// 官方界面字体：Regular 方正兰亭黑（FZLTHJW），Bold 方正兰亭中黑（FZLTZHJW），由 import_official_fonts.py 生成。
inline UFont* OfficialFont()
{
	static TStrongObjectPtr<UFont> Font(LoadObject<UFont>(nullptr, TEXT("/Game/UI/Fonts/F_DeltaForce.F_DeltaForce")));
	return Font.Get();
}

// 官方字体的 Slate 字体信息；资源缺失时退回引擎默认字体。
inline FSlateFontInfo Font(float Size, bool bBold = false)
{
	if (UFont* Official = OfficialFont()) return FSlateFontInfo(Official, Size, bBold ? TEXT("Bold") : TEXT("Regular"));
	return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
}

// 把控件里所有文本换成官方字体，保留原字号、颜色与描边；原字重为粗体系时用 Bold。
inline void ApplyFont(UUserWidget* Widget)
{
	UFont* Official = OfficialFont();
	if (!Official || !Widget || !Widget->WidgetTree) return;
	Widget->WidgetTree->ForEachWidget([Official](UWidget* Child)
	{
		auto* Text = Cast<UTextBlock>(Child);
		if (!Text) return;
		FSlateFontInfo Info = Text->GetFont();
		const FString Face = Info.TypefaceFontName.ToString();
		const bool bBold = Face.Contains(TEXT("Bold")) || Face.Contains(TEXT("Black")) || Face.Contains(TEXT("Heavy"));
		Info.FontObject = Official;
		Info.TypefaceFontName = bBold ? TEXT("Bold") : TEXT("Regular");
		Text->SetFont(Info);
	});
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
