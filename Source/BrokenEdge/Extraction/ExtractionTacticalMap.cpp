#include "ExtractionTacticalMap.h"
#include "ExtractionUIStyle.h"
#include "ExtractionMapDefinition.h"
#include "ExtractionExit.h"
#include "ExtractionState.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

class SExtractionTacticalMap : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SExtractionTacticalMap)
		{
		}

		SLATE_ARGUMENT(UExtractionTacticalMap*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& A) { Owner = A._Owner; }
	virtual void Tick(const FGeometry&, double, float) override { Invalidate(EInvalidateWidgetReason::Paint); }
	virtual FVector2D ComputeDesiredSize(float) const override { return {1280, 720}; }

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Draw,
	                      int32 Layer, const FWidgetStyle&, bool) const override
	{
		if (!Owner.IsValid() || !Owner->Definition) return Layer;
		const auto* D = Owner->Definition.Get();
		auto* PC = Owner->GetOwningPlayer();
		if (!PC) return Layer;
		const float Scale = ExtractionUIStyle::Scale(G);
        const FVector2D CanvasOffset = ExtractionUIStyle::Offset(G);
		auto Box = [&](FVector2D P, FVector2D S, FLinearColor C, int32 Z = 0)
		{
			FSlateDrawElement::MakeBox(Draw, Layer + Z, G.ToPaintGeometry(S * Scale, FSlateLayoutTransform(CanvasOffset + P * Scale)),
			                           FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, C);
		};
		auto Text = [&](FVector2D P, const FString& Value, int32 Font = 15, FLinearColor C = FLinearColor::White)
		{
			FSlateDrawElement::MakeText(Draw, Layer + 3,
			                            G.ToPaintGeometry(FVector2D(1000, 40) * Scale,
			                                              FSlateLayoutTransform(CanvasOffset + P * Scale)),
			                            Value, FCoreStyle::GetDefaultFontStyle(
				                            "Regular", FMath::Max(8, FMath::RoundToInt(Font * Scale))),
			                            ESlateDrawEffect::None, C);
		};
		const FVector2D Span = D->OverviewMax - D->OverviewMin;
		const double Zoom = FMath::Min(840. / FMath::Max(1., Span.Y), 520. / FMath::Max(1., Span.X));
		const FVector2D Origin = FVector2D(70, 120) + (FVector2D(840, 520) - FVector2D(Span.Y, Span.X) * Zoom) * .5;
		auto Project = [&](FVector2D P)
		{
			return Origin + FVector2D(P.Y - D->OverviewMin.Y, D->OverviewMax.X - P.X) * Zoom;
		};
		Box({35, 35}, {1210, 650}, FLinearColor(.009f, .02f, .024f, .98f));
		Text({65, 62},TEXT("航天基地  /  战术地图"), 26);
		Text({65, 660},TEXT("M / Esc 关闭   ·   ↑ 北   ·   建筑平面投影，标记高度以当前位置为基准"), 14);
		Box({65, 115}, {850, 530}, FLinearColor(.025f, .045f, .048f));
		for (int32 I = 1; I < 10; ++I)
		{
			Box({65.f + I * 85, 115}, {1, 530}, FLinearColor(.04f, .065f, .068f));
			Box({65, 115.f + I * 53}, {850, 1}, FLinearColor(.04f, .065f, .068f));
		}
		for (int32 I = 0; I + 1 < D->FloorBounds.Num(); I += 2)
		{
			const auto A = Project(D->FloorBounds[I]), B = Project(D->FloorBounds[I + 1]);
			const FVector2D P(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y));
			const FVector2D S(FMath::Max(1., FMath::Abs(B.X - A.X)), FMath::Max(1., FMath::Abs(B.Y - A.Y)));
			Box(P, S, FLinearColor(.04f, .07f, .07f, .3f), 1);
			const FLinearColor Edge(.14f, .23f, .22f, .7f);
			Box(P, {S.X, 1}, Edge, 1);
			Box(P, {1, S.Y}, Edge, 1);
			Box(P + FVector2D(0, S.Y), {S.X, 1}, Edge, 1);
			Box(P + FVector2D(S.X, 0), {1, S.Y}, Edge, 1);
		}
		const auto* GS = PC->GetWorld()->GetGameState<AExtractionGameState>();
		const auto* Local = PC->GetPlayerState<AExtractionPlayerState>();
		const float LocalZ = PC->GetPawn() ? PC->GetPawn()->GetActorLocation().Z : 0;
		int32 Row = 0;
		for (TActorIterator<AExtractionExit> It(PC->GetWorld()); It; ++It)
		{
			const bool Available = It->Available(GS ? GS->GetServerWorldTimeSeconds() : 0);
			const FLinearColor C = Available ? FLinearColor(.25f, .9f, .65f) : FLinearColor(.7f, .45f, .25f);
			const auto P = Project(FVector2D(It->GetActorLocation()));
			Box(P - FVector2D(5), {10, 10}, C, 2);
			Text(P + FVector2D(8, -10), FString::FromInt(Row + 1), 14, C);
			Text({940, 140.f + Row * 75}, FString::Printf(TEXT("%d  %s"), Row + 1, *It->Definition.Label.ToString()),
			     16, C);
			FString Detail = Available
				                ? TEXT("可以撤离")
				                : It->UnavailableReason(GS ? GS->GetServerWorldTimeSeconds() : 0);
			if (It->Definition.bRequireEmptyPack) Detail += TEXT("  ·  需清空背包");
			Text({940, 166.f + Row * 75}, Detail, 13);
			++Row;
		}
		int32 MemberRow = 0;
		// 撤离点数量由数据决定，小队列表必须跟着往下排，否则四个以上出口会压在小队标题上。
		const float ListBottom = 140.f + Row * 75.f + 28.f;
		Text({940, ListBottom},TEXT("小队位置"), 18);
		for (TActorIterator<APawn> It(PC->GetWorld()); It; ++It)
		{
			const auto* PS = It->GetPlayerState<AExtractionPlayerState>();
			if (!PS || !Local || PS->ExtractionSquad != Local->ExtractionSquad || Local->ExtractionSquad <= 0) continue;
			const auto World = It->GetActorLocation();
			const auto P = Project(FVector2D(World));
			const FLinearColor C = PS == Local ? FLinearColor::White : FLinearColor(.25f, .7f, 1);
			Box(P - FVector2D(4), {8, 8}, C, 2);
			Text({940, ListBottom + 35.f + MemberRow++ * 32},
			     FString::Printf(TEXT("%s  %+.0fm"), PS == Local ? TEXT("你") : *PS->GetPlayerName().Left(12),
			                     (World.Z - LocalZ) / 100), 13, C);
		}
		return Layer + 4;
	}

private:
	TWeakObjectPtr<UExtractionTacticalMap> Owner;
};

TSharedRef<SWidget> UExtractionTacticalMap::RebuildWidget()
{
	Definition = LoadObject<UExtractionMapDefinition>(
		nullptr,TEXT("/Game/Extraction/Maps/DA_SpaceCenterGameplay.DA_SpaceCenterGameplay"));
	return SNew(SExtractionTacticalMap).Owner(this);
}
