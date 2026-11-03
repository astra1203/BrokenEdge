#pragma once
#include "ExtractionItems.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "UObject/StrongObjectPtr.h"
#include "Styling/CoreStyle.h"
#include "Engine/Font.h"
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

// 项目自有界面配色：石墨底色 + 琥珀强调色；品质色按白/绿/蓝/紫/金/红六级。
inline FLinearColor Hex(const TCHAR* Value, float Alpha = 1.f)
{
	FLinearColor Color = FLinearColor::FromSRGBColor(FColor::FromHex(Value));
	Color.A = Alpha;
	return Color;
}
inline FLinearColor Accent(float Alpha = 1.f) { return Hex(TEXT("F2A93B"), Alpha); }
inline FLinearColor Muted() { return Hex(TEXT("8A939D")); }

// 把品质等级限制到颜色表有效范围，避免异常数据越界。
inline FLinearColor Rarity(int32 Level)
{
	static const FLinearColor Colors[] = {
		Hex(TEXT("8C9097")), Hex(TEXT("5DBB72")), Hex(TEXT("4A9BE8")),
		Hex(TEXT("A77BEA")), Hex(TEXT("EDB341")), Hex(TEXT("EA5A50"))
	};
	return Colors[FMath::Clamp(Level, 0, 5)];
}

// 界面字体用引擎自带的 /Engine/EngineFonts/Roboto：字母数字是 Roboto，中文由它配置的后备字体 DroidSansFallback 补齐。
// 必须引用 UFont 资源：控件资源存不下 FCoreStyle 默认字体的运行时数据，加载后会退成只能画方框的保底字体。
inline UFont* UIFont()
{
	static TStrongObjectPtr<UFont> Font(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")));
	return Font.Get();
}

inline FSlateFontInfo Font(float Size, bool bBold = false)
{
	if (UFont* Asset = UIFont()) return FSlateFontInfo(Asset, Size, bBold ? TEXT("Bold") : TEXT("Regular"));
	return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
}

// 把控件里所有文本换成项目字体，保留字号、颜色与描边；原字重为粗体系时用 Bold。
inline void ApplyFont(UUserWidget* Widget)
{
	UFont* Asset = UIFont();
	if (!Asset || !Widget || !Widget->WidgetTree) return;
	Widget->WidgetTree->ForEachWidget([Asset](UWidget* Child)
	{
		auto* Text = Cast<UTextBlock>(Child);
		if (!Text) return;
		FSlateFontInfo Info = Text->GetFont();
		const FString Face = Info.TypefaceFontName.ToString();
		const bool bBold = Face.Contains(TEXT("Bold")) || Face.Contains(TEXT("Black")) || Face.Contains(TEXT("Heavy"));
		Info.FontObject = Asset;
		Info.TypefaceFontName = bBold ? TEXT("Bold") : TEXT("Regular");
		Text->SetFont(Info);
	});
}

// 圆角面板笔刷：填充色 + 细描边，界面不依赖任何贴图。
inline FSlateBrush Panel(FLinearColor Fill, float Radius = 4.f, FLinearColor Outline = FLinearColor::Transparent,
                         float OutlineWidth = 1.f)
{
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(Fill);
	Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(Radius, Radius, Radius, Radius), FSlateColor(Outline),
	                                                   OutlineWidth);
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	return Brush;
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
