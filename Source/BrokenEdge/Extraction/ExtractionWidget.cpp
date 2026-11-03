#include "ExtractionWidget.h"
#include "ExtractionUIStyle.h"
#include "ExtractionPlayerController.h"
#include "BrokenEdge/Traversal/ExtractionTraversalComponent.h"
#include "ExtractionRegionStream.h"
#include "ExtractionHealth.h"
#include "ExtractionMeleeComponent.h"
#include "ExtractionInteractionComponent.h"
#include "ExtractionAbilities.h"
#include "ExtractionSmoke.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionAccountComponent.h"
#include "ExtractionExit.h"
#include "EngineUtils.h"
#include "ExtractionState.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Fonts/FontMeasure.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/GameplayStaticsTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"

void UExtractionWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	ExtractionUIStyle::ApplyFont(this);
	Previous->OnClicked.AddUniqueDynamic(this, &ThisClass::PreviousClicked);
	Next->OnClicked.AddUniqueDynamic(this, &ThisClass::NextClicked);
	Leave->OnClicked.AddUniqueDynamic(this, &ThisClass::LeaveClicked);
}

// 读取 GameState、PlayerState 和本人账号快照更新倒计时、撤离点、任务、生命与武器信息。
// 使用同步服务器时间计算剩余秒数，进度条到零本身不会触发撤离结算。
void UExtractionWidget::Refresh()
{
	const auto* PC = GetOwningPlayer<AExtractionPlayerController>();
	const auto* GS = GetWorld()->GetGameState<AExtractionGameState>();
	const auto* PS = PC ? PC->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!PC || !GS || !PS) return;
	FVector Eye;
	FRotator View;
	PC->GetPlayerViewPoint(Eye, View);
	// 记下相机供 NativePaint 换算受击方向。
	ViewEye = Eye;
	ViewYaw = View.Yaw;
	const int32 Heading = FMath::RoundToInt(FRotator::ClampAxis(View.Yaw)) % 360;
	// Coordinate compass uses world +X as north; source-map georeferencing is separate.
	const TCHAR* Directions[] = {
		TEXT("N"), TEXT("NE"), TEXT("E"), TEXT("SE"), TEXT("S"), TEXT("SW"), TEXT("W"), TEXT("NW")
	};
	Compass->SetText(
		FText::FromString(FString::Printf(TEXT("—   %s   %03d°   —"), Directions[((Heading + 22) / 45) % 8], Heading)));
	// 只给"在烟里"的信号太生硬，改为按距烟心的深度渐变：烟心最浓 .55，边缘接近 0。
	const float Haze = AExtractionSmoke::Haze(GetWorld(), Eye);
	SmokeHaze->SetVisibility(Haze > .01f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	SmokeHaze->SetBrushColor(FLinearColor(.34f, .34f, .34f, Haze * .55f));
	const int32 Seconds = FMath::Max(0, FMath::CeilToInt(GS->Deadline - GS->GetServerWorldTimeSeconds()));
	FString Text;
	if (GS->Phase == EExtractionPhase::Waiting) Text = TEXT("等待其他玩家加载…");
	else if (GS->Phase == EExtractionPhase::Ended)
		Text = FString::Printf(
			TEXT("%s\n%d 秒后返回准备大厅"), *GS->EndReason, Seconds);
	else
	{
		const TCHAR* Hint = PS->Life == EExtractionLife::Alive
			                    ? TEXT("Esc 打开对局菜单")
			                    : PS->Life == EExtractionLife::Downed
			                    ? TEXT("倒地待救援 · Esc 打开菜单")
			                    : TEXT("本次行动已结束");
		// 分栏 HUD 把时间和存活人数放进独立大字段，整段文本只留提示。
		Text = RaidTimer
			       ? FString(Hint)
			       : FString::Printf(TEXT("航天基地 · 小队行动\n剩余 %02d:%02d · 存活 %d\n%s"), Seconds / 60, Seconds % 60,
			                         GS->AlivePlayers, Hint);
	}
	if (RaidTimer)
	{
		const bool bActive = GS->Phase == EExtractionPhase::Active;
		RaidTimer->SetText(FText::FromString(bActive ? FString::Printf(TEXT("%02d:%02d"), Seconds / 60, Seconds % 60)
			                                     : TEXT("--:--")));
		// 最后五分钟转红，提醒尽快撤离。
		RaidTimer->SetColorAndOpacity(FSlateColor(bActive && Seconds <= 300
			                                          ? ExtractionUIStyle::Hex(TEXT("EA5A50"))
			                                          : FLinearColor::White));
	}
	if (RaidInfo)
		RaidInfo->SetText(FText::FromString(FString::Printf(TEXT("航天基地  ·  存活 %d  ·  击杀守卫 %d"),
		                                                    GS->AlivePlayers, PS->RaidProgress.GuardKills)));
	if (!GS->StorageError.IsEmpty()) Text = GS->StorageError;
	if (PC->ExtractionAccount && PS->Life == EExtractionLife::Alive)
	{
		const auto& Progression = PC->ExtractionAccount->Profile.Progression;
		const auto* Rules = GetDefault<UExtractionProgressionRules>();
		for (int32 Q = 0; Q < Progression.Quests.Num(); ++Q)
			if (Progression.Quests[Q].Accepted && !Progression.Quests[Q].Claimed)
			{
				Text += TEXT("\n任务 · ") + Rules->Quests[Q].Label;
				if (Q == 1)
					Text += FString::Printf(TEXT(" %d / %d（本局 +%d，撤离保存）"), Progression.Quests[Q].Progress,
					                        Rules->Quests[Q].Target, PS->RaidProgress.GuardKills);
				else Text += Q == 0 ? TEXT(" · 成功撤离后完成") : TEXT(" · 文件带出后回大厅提交");
			}
	}
	FString ExitsText;
	FString CountdownText;
	float ExitFraction = 0;
	bool bShowExitProgress = false;
	if (PS->Life == EExtractionLife::Alive && GS->Phase == EExtractionPhase::Active && PC->GetPawn())
	{
		for (TActorIterator<AExtractionExit> It(GetWorld()); It; ++It)
		{
			const FString Reason = It->UnavailableReason(GS->GetServerWorldTimeSeconds());
			FString Rule;
			if (It->Definition.bRequireEmptyPack) Rule = TEXT(" · 需清空背包");
			ExitsText += FString::Printf(TEXT("%s%s\n%.0f 米 · %s\n\n"), *It->Definition.Label.ToString(), *Rule,
			                             FVector::Dist2D(PC->GetPawn()->GetActorLocation(), It->GetActorLocation()) /
			                             100,
			                             Reason.IsEmpty() ? TEXT("可撤离") : *Reason);
			// 匹配当前撤离点 ID 后，才用其停留时长计算进度；显示以服务器发布的截止时刻为准。
			if (PS->ExtractAt > 0 && PS->ExtractExitId == It->Definition.Id)
			{
				const double Remaining = FMath::Max(0.0, PS->ExtractAt - GS->GetServerWorldTimeSeconds());
				// Keep the point and timer on one line so the existing two-line HUD slot stays above its progress bar.
				CountdownText = Remaining > 0
					? FString::Printf(TEXT("%s · 正在撤离 %.1f\n请留在撤离区域内"), *It->Definition.Label.ToString(), Remaining)
					: It->Definition.Label.ToString() + TEXT("\n等待服务器确认撤离");
				ExitFraction = FMath::Clamp(1.f - Remaining / It->Definition.HoldSeconds, 0., 1.);
				bShowExitProgress = Remaining > 0;
			}
			else if (!PS->ExtractBlockedReason.IsEmpty() && PS->ExtractExitId == It->Definition.Id)
			{
				CountdownText = It->Definition.Label.ToString() + TEXT("\n") + PS->ExtractBlockedReason;
				ExitFraction = 0;
			}
		}
	}
	ExitStatus->SetText(FText::FromString(ExitsText));
	ExtractionCountdown->SetText(FText::FromString(CountdownText));
	ExtractionProgress->SetPercent(ExitFraction);
	ExtractionProgress->SetVisibility(bShowExitProgress
		                                  ? ESlateVisibility::HitTestInvisible
		                                  : ESlateVisibility::Collapsed);
	if (ExtractionCard)
		ExtractionCard->SetVisibility(CountdownText.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	if (PC->ExtractionAccount && PC->ExtractionAccount->Profile.Result.Outcome != EExtractionOutcome::None)
	{
		const auto& Result = PC->ExtractionAccount->Profile.Result;
		Text += TEXT("\n") + UExtractionPersistence::OutcomeLabel(Result.Outcome);
		if (!Result.ProgressMessage.IsEmpty()) Text += TEXT("\n") + Result.ProgressMessage;
		// 先算总价值放在清单前面，一眼看到这局带出了多少。
		int64 TotalValue = 0;
		for (const auto& I : Result.Items)
			if (const auto* D = UExtractionItemDefinition::Get(I.Definition))
				TotalValue += int64(D->SellValue) * FMath::Max(1, I.Quantity);
		Text += Result.Outcome == EExtractionOutcome::Extracted
			        ? TEXT("\n带出总价值 ¥") + FText::AsNumber(TotalValue).ToString() + TEXT("\n已带出：")
			        : TEXT("\n本次物资：");
		for (FName Id : UExtractionItemDefinition::Ids())
		{
			int32 Count = 0;
			for (const auto& I : Result.Items) if (I.Definition == Id) Count += I.Quantity;
			if (Count)
			{
				const auto* D = UExtractionItemDefinition::Get(Id);
				Text += FString::Printf(TEXT("\n%s × %d"), *D->Label.ToString(), Count);
				if (D->SellValue > 0) Text += TEXT("  ¥") + FText::AsNumber(int64(D->SellValue) * Count).ToString();
			}
		}
		Text += FString::Printf(TEXT("\n领取区：%d 件"), Result.OverflowCount);
	}
	if (const auto* Region = AExtractionRegionStream::Find(PC->GetWorld()); Region && !Region->GetStatus().IsEmpty())
		Text += TEXT("\n") + Region->GetStatus();
	if (PC->bExtractionSpectating)
		Text += PC->SpectatedPlayer ? TEXT("\n正在观战：") + PC->SpectatedPlayer->GetPlayerName() : TEXT("\n暂无存活玩家可供观战");
	const double Now = GS->GetServerWorldTimeSeconds();
	Text += FString::Printf(TEXT("\n小队 %d · 队友免伤"), PS->ExtractionSquad);
	for (APlayerState* State : GS->PlayerArray)
		if (const auto* Mate = Cast<AExtractionPlayerState>(State); Mate && Mate != PS && Mate->ExtractionSquad == PS->
			ExtractionSquad)
		{
			const TCHAR* Label = Mate->Life == EExtractionLife::Alive
				                     ? TEXT("存活")
				                     : Mate->Life == EExtractionLife::Downed
				                     ? TEXT("倒地")
				                     : Mate->Life == EExtractionLife::Extracted
				                     ? TEXT("已撤离")
				                     : TEXT("已离场");
			Text += FString::Printf(TEXT("\n%s · %s"), *Mate->GetPlayerName(), Label);
			if (auto* C = Cast<ABrokenEdgeCharacter>(Mate->GetPawn()); C && Mate->Life == EExtractionLife::Alive)
				Text
					+= FString::Printf(TEXT(" · %.0f HP"), C->GetHealth());
		}
	if (PS->Life == EExtractionLife::Downed)
		Text += FString::Printf(
			TEXT("\n你已倒地 · %.0f 生命 · %.1f 秒后阵亡\n缓慢移动至掩体，等待队友救援"), PS->DownHealth, FMath::Max(0.0, PS->BleedAt - Now));
	if (auto* C = Cast<ABrokenEdgeCharacter>(PC->GetPawn()))
	{
		const FString TraversalPrompt = C->GetTraversal()->Prompt();
		if (!TraversalPrompt.IsEmpty()) Text += TEXT("\n") + TraversalPrompt;
		const auto* A = C->GetExtractionAbilities();
		if (PS->Life == EExtractionLife::Alive)
		{
			Text += FString::Printf(TEXT("\nZ 激素枪(对准队友，否则自用) %.0fs · X 蜂巢烟雾(Alt+X 低抛) %.0fs"),
			                        FMath::Max(0.0, A->HealReadyAt - Now), FMath::Max(0.0, A->SmokeReadyAt - Now));
			if (C->GetExtractionHealth()->Painkiller) Text += TEXT("\n激素生效 · 止痛中，骨折暂不影响移动");
			if (A->RescueFocus()) Text += TEXT("\n按住 E 救援队友 · 5 秒");
		}
		if (A->Action != EExtractionAction::None)
			Text += FString::Printf(
				TEXT("\n%s · %.1f 秒"),
				A->Action == EExtractionAction::Revive
					? TEXT("正在救援")
					: A->Action == EExtractionAction::Heal
					? (A->Target && A->Target != C ? TEXT("激素枪 · 射向队友") : TEXT("激素枪 · 自我注射"))
					: TEXT("投放蜂巢烟雾"), FMath::Max(0.0, A->ActionEnd - Now));
		if (A->Rescuer)
			Text += FString::Printf(
				TEXT("\n队友正在救援 · %.1f 秒"), FMath::Max(0.0, A->Rescuer->GetExtractionAbilities()->ActionEnd - Now));
	}
	Status->SetText(FText::FromString(Text));
	FString VitalText;
	FString WeaponText;
	HealthProgress->SetVisibility(PC->GetPawn() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (auto* ControlledCharacter = Cast<ABrokenEdgeCharacter>(PC->GetPawn()))
	{
		const float HealthFraction = ControlledCharacter->GetHealth() / FMath::Max(1.f, ControlledCharacter->GetMaxHealth());
		HealthProgress->SetPercent(HealthFraction);
		// 生命条按剩余比例由绿转黄再转红。
		HealthProgress->SetFillColorAndOpacity(HealthFraction > .6f
			                                       ? ExtractionUIStyle::Hex(TEXT("5DBB72"))
			                                       : HealthFraction > .3f
			                                       ? ExtractionUIStyle::Accent()
			                                       : ExtractionUIStyle::Hex(TEXT("EA5A50")));
		if (HealthText) HealthText->SetText(FText::AsNumber(FMath::CeilToInt(ControlledCharacter->GetHealth())));
		else
			VitalText = FString::Printf(TEXT("生命 %d / %d\n"),
			                            FMath::CeilToInt(ControlledCharacter->GetHealth()),
			                            FMath::CeilToInt(ControlledCharacter->GetMaxHealth()));
		if (const auto* Inv = ControlledCharacter->GetExtractionInteraction()->Inventory())
		{
			for (int32 ArmorSlot = 2; ArmorSlot <= 3; ++ArmorSlot)
			{
				const auto* Armor = Inv->Contents.InSlot(ArmorSlot);
				VitalText += (ArmorSlot == 2 ? TEXT("头盔 ") : TEXT("  胸甲 ")) + (Armor
					                                                               ? ExtractionItems::Details(*Armor)
					                                                               : FString(TEXT("无")));
			}
			VitalText += TEXT("\n");
		}
		const auto* Trauma = ControlledCharacter->GetExtractionHealth();
		if (Trauma->Bleeding) VitalText += TEXT("流血 · 需绷带  ");
		if (Trauma->LegFracture) VitalText += TEXT("腿部骨折 · 需夹板");
		if (Trauma->Bleeding || Trauma->LegFracture) VitalText += TEXT("\n");
		// 分栏 HUD：名称、弹匣大字、备弹与弹种各占一格；旧布局仍拼成一段。
		FString Name, Rounds, Detail;
		bool bLowAmmo = false;
		if (ControlledCharacter->Melee->Active)
		{
			Name = TEXT("战术刀");
			Detail = TEXT("左键轻击 / 右键重击");
		}
		else if (auto* Weapon = ControlledCharacter->GetEquippedWeapon())
		{
			const auto* Definition = UExtractionItemDefinition::Get(Weapon->ExtractionDefinition);
			const auto* Ammo = UExtractionItemDefinition::Get(Weapon->ExtractionAmmoType);
			Name = Definition ? Definition->Label.ToString() : TEXT("步枪");
			Rounds = FString::FromInt(Weapon->GetAmmo());
			const int32 Carried = ControlledCharacter->GetCombat()->GetCarriedAmmo();
			Detail = FString::Printf(TEXT("/ %d\n%s"), Carried, Ammo ? *Ammo->Label.ToString() : TEXT("弹药"));
			// 弹匣剩三成以下转红并提示换弹；弹匣和备弹都空时提示弹药耗尽。
			bLowAmmo = Weapon->GetAmmo() <= FMath::Max(1, FMath::FloorToInt(Weapon->GetMagCapacity() * .3f));
			if (Weapon->GetAmmo() == 0 && Carried == 0) Detail = TEXT("弹药耗尽\n") + Detail;
			else if (bLowAmmo && Carried > 0) Detail = TEXT("R 换弹\n") + Detail;
		}
		else
		{
			Name = TEXT("未持枪");
			Detail = TEXT("靠近武器按 E 拾取");
		}
		if (WeaponName && AmmoText)
		{
			WeaponName->SetText(FText::FromString(Name));
			AmmoText->SetText(FText::FromString(Rounds));
			AmmoText->SetColorAndOpacity(FSlateColor(bLowAmmo ? ExtractionUIStyle::Hex(TEXT("EA5A50")) : FLinearColor::White));
			AmmoText->SetVisibility(Rounds.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
			WeaponText = Detail;
		}
		else WeaponText = Name + TEXT("\n") + (Rounds.IsEmpty() ? Detail : Rounds + TEXT(" ") + Detail);
	}
	Vitals->SetText(FText::FromString(VitalText));
	WeaponStatus->SetText(FText::FromString(WeaponText));
	Previous->SetVisibility(PC->bExtractionSpectating ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	Next->SetVisibility(Previous->GetVisibility());
	Previous->SetIsEnabled(PC->SpectatedPlayer != nullptr);
	Next->SetIsEnabled(PC->SpectatedPlayer != nullptr);
	Leave->SetVisibility(PC->IsMenuOpen() || PC->bExtractionSpectating || GS->Phase == EExtractionPhase::Ended
		                     ? ESlateVisibility::Visible
		                     : ESlateVisibility::Collapsed);
}

int32 UExtractionWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
                                     FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style,
                                     bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, Geometry, Culling, Out, LayerId, Style, bParentEnabled) + 1;
	const auto* PC = GetOwningPlayer<AExtractionPlayerController>();
	if (!PC || !GetWorld()) return LayerId;
	const double Now = GetWorld()->GetTimeSeconds();
	const FVector2D Center = Geometry.GetLocalSize() * .5f;
	const auto Lines = [&](const TArray<FVector2D>& Points, FLinearColor Color, float Thickness)
	{
		FSlateDrawElement::MakeLines(Out, LayerId, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color,
		                             true, Thickness);
	};

	// 命中标记：准星四周四条斜线；命中白、击倒琥珀、击杀红，击杀停留更久且略大。
	const double HitAge = Now - PC->LastHitAt;
	const float HitSeconds = PC->LastHitResult == 2 ? .6f : PC->LastHitResult == 1 ? .45f : .22f;
	if (HitAge >= 0 && HitAge < HitSeconds)
	{
		const float Fade = 1.f - float(HitAge / HitSeconds);
		const FLinearColor Color = (PC->LastHitResult == 2
			                            ? ExtractionUIStyle::Hex(TEXT("EA5A50"))
			                            : PC->LastHitResult == 1
			                            ? ExtractionUIStyle::Accent()
			                            : FLinearColor::White).CopyWithNewOpacity(.95f * Fade);
		const float Gap = PC->LastHitResult == 2 ? 8.f : 6.f;
		const float Length = PC->LastHitResult == 2 ? 13.f : 9.f;
		for (const FVector2D Dir : {FVector2D(1, 1), FVector2D(-1, 1), FVector2D(1, -1), FVector2D(-1, -1)})
		{
			const FVector2D Unit = Dir.GetSafeNormal();
			Lines({Center + Unit * Gap, Center + Unit * (Gap + Length)}, Color, PC->LastHitResult == 2 ? 3.f : 2.f);
		}
	}

	// 受击方向：以准星为圆心的红色弧段，指向伤害来源，1.6 秒内淡出。
	constexpr double DamageSeconds = 1.6;
	for (const auto& [At, From] : PC->DamageSources)
	{
		const double Age = Now - At;
		if (Age < 0 || Age >= DamageSeconds) continue;
		const FVector ToSource = From - ViewEye;
		// 屏幕正上方为视线前方，顺时针为向右。
		const float Angle = FMath::DegreesToRadians(FRotator::NormalizeAxis(ToSource.Rotation().Yaw - ViewYaw));
		TArray<FVector2D> Arc;
		constexpr float Radius = 150.f, Half = .32f;
		for (int32 Step = 0; Step <= 12; ++Step)
		{
			const float A = Angle - Half + 2 * Half * Step / 12.f;
			Arc.Add(Center + FVector2D(FMath::Sin(A), -FMath::Cos(A)) * Radius);
		}
		const float Fade = 1.f - float(Age / DamageSeconds);
		Lines(Arc, ExtractionUIStyle::Hex(TEXT("EA5A50"), .85f * Fade), 5.f);
	}

	// 击倒/击杀提示：准星下方居中，4 秒内淡出，最新的一条在最上面。
	constexpr double NoticeSeconds = 4.0;
	const FSlateFontInfo Font = ExtractionUIStyle::Font(16, true);
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	float Y = Center.Y + 70.f;
	for (int32 Index = PC->CombatNotices.Num() - 1; Index >= 0; --Index)
	{
		const auto& [At, Text] = PC->CombatNotices[Index];
		const double Age = Now - At;
		if (Age < 0 || Age >= NoticeSeconds) continue;
		const float Fade = FMath::Clamp(float((NoticeSeconds - Age) / .6), 0.f, 1.f);
		const FVector2D Size = Measure->Measure(Text, Font);
		const FLinearColor Color = Text.StartsWith(TEXT("击杀"))
			                           ? ExtractionUIStyle::Hex(TEXT("EA5A50"))
			                           : ExtractionUIStyle::Accent();
		FSlateDrawElement::MakeText(Out, LayerId, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(
			                            FVector2D(Center.X - Size.X * .5f, Y))), Text, Font, ESlateDrawEffect::None,
		                            Color.CopyWithNewOpacity(Fade));
		Y += Size.Y + 4.f;
	}

	// 体力条：底部居中，体力不满时显示，低于起跑门槛时变红。
	if (const auto* Runner = Cast<ABrokenEdgeCharacter>(PC->GetPawn()); Runner && Runner->GetStaminaFraction() < .999f)
	{
		const float Fraction = FMath::Clamp(Runner->GetStaminaFraction(), 0.f, 1.f);
		const FVector2D BarSize(220.f, 4.f);
		const FVector2D BarAt(Center.X - BarSize.X * .5f, Geometry.GetLocalSize().Y - 54.f);
		static const FSlateBrush BarBrush = ExtractionUIStyle::Panel(FLinearColor::White, 2.f);
		FSlateDrawElement::MakeBox(Out, LayerId, Geometry.ToPaintGeometry(BarSize, FSlateLayoutTransform(BarAt)), &BarBrush,
		                           ESlateDrawEffect::None, FLinearColor(1, 1, 1, .15f));
		const bool bLow = Runner->GetStamina() < GetDefault<UExtractionRules>()->StaminaSprintMin;
		FSlateDrawElement::MakeBox(Out, LayerId, Geometry.ToPaintGeometry(FVector2D(BarSize.X * Fraction, BarSize.Y),
			                           FSlateLayoutTransform(BarAt)), &BarBrush, ESlateDrawEffect::None,
		                           bLow ? ExtractionUIStyle::Hex(TEXT("EA5A50")) : FLinearColor(1, 1, 1, .85f));
	}

	// 手雷瞄准：按住 G 时按服务器同样的出手点、仰角与初速预测轨迹，画出弧线和预计落点的满伤圈。
	if (PC->bAimingGrenade)
		if (const APawn* Thrower = PC->GetPawn())
		{
			const auto* Rules = GetDefault<UExtractionRules>();
			FVector Eye;
			FRotator Aim;
			Thrower->GetActorEyesViewPoint(Eye, Aim);
			Aim.Pitch = FMath::Clamp(Aim.Pitch + Rules->FragThrowPitch, -89.f, 89.f);
			FPredictProjectilePathParams Params(5.f, Eye + Aim.Vector() * 40.f,
			                                    Aim.Vector() * (PC->bAimingGrenadeLow ? Rules->FragThrowLowSpeed : Rules->FragThrowSpeed)
			                                    + Thrower->GetVelocity(), Rules->FragFuseSeconds, ECC_Visibility,
			                                    const_cast<APawn*>(Thrower));
			Params.SimFrequency = 20.f;
			FPredictProjectilePathResult Result;
			UGameplayStatics::PredictProjectilePath(GetWorld(), Params, Result);
			auto Project = [&](const FVector& World, FVector2D& Out2D)
			{
				return UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
					const_cast<AExtractionPlayerController*>(PC), World, Out2D, false);
			};
			TArray<FVector2D> Arc;
			for (const auto& Point : Result.PathData)
				if (FVector2D At; Project(Point.Location, At)) Arc.Add(At);
			if (Arc.Num() > 1) Lines(Arc, ExtractionUIStyle::Accent(.85f), 2.f);
			const FVector Landing = Result.HitResult.bBlockingHit
				                        ? Result.HitResult.ImpactPoint
				                        : Result.LastTraceDestination.Location;
			TArray<FVector2D> Ring;
			for (int32 Step = 0; Step <= 32; ++Step)
			{
				const float A = 2 * PI * Step / 32.f;
				if (FVector2D At; Project(Landing + FVector(FMath::Cos(A), FMath::Sin(A), 0) * Rules->FragFullRadius + FVector(0, 0, 5), At))
					Ring.Add(At);
			}
			if (Ring.Num() > 2) Lines(Ring, ExtractionUIStyle::Hex(TEXT("EA5A50"), .85f), 2.f);
		}

	// 撤离点标记：存活且对局进行中时，把每个撤离点投影到屏幕，可撤离为绿色、暂不可用为灰色；屏幕外贴边。
	const auto* GS = GetWorld()->GetGameState<AExtractionGameState>();
	const auto* Me = PC->GetPlayerState<AExtractionPlayerState>();
	if (GS && Me && Me->Life == EExtractionLife::Alive && GS->Phase == EExtractionPhase::Active && !PC->bExtractionSpectating)
	{
		const FVector2D ScreenSize = Geometry.GetLocalSize();
		const FSlateFontInfo ExitFont = ExtractionUIStyle::Font(12, true);
		for (TActorIterator<AExtractionExit> It(GetWorld()); It; ++It)
		{
			const bool bOpen = It->UnavailableReason(GS->GetServerWorldTimeSeconds()).IsEmpty();
			const FVector Location = It->GetActorLocation() + FVector(0, 0, 200);
			FVector2D At;
			if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
				const_cast<AExtractionPlayerController*>(PC), Location, At, false))
			{
				const float Side = FRotator::NormalizeAxis((Location - ViewEye).Rotation().Yaw - ViewYaw) > 0 ? 1.f : -1.f;
				At = FVector2D(Side > 0 ? ScreenSize.X - 40.f : 40.f, ScreenSize.Y * .5f);
			}
			At.X = FMath::Clamp(At.X, 40.f, ScreenSize.X - 40.f);
			At.Y = FMath::Clamp(At.Y, 80.f, ScreenSize.Y - 140.f);
			const FLinearColor Color = bOpen ? ExtractionUIStyle::Hex(TEXT("5DBB72"), .9f) : FLinearColor(.7f, .72f, .75f, .55f);
			// 向下的小三角，下方写“名称 · 距离”。
			Lines({At + FVector2D(-7, -6), At + FVector2D(7, -6), At + FVector2D(0, 5), At + FVector2D(-7, -6)}, Color, 2.f);
			const FString Label = FString::Printf(TEXT("%s · %.0fm"), *It->Definition.Label.ToString(),
			                                      FVector::Dist(It->GetActorLocation(), ViewEye) / 100);
			const FVector2D LabelSize = Measure->Measure(Label, ExitFont);
			FSlateDrawElement::MakeText(Out, LayerId, Geometry.ToPaintGeometry(LabelSize, FSlateLayoutTransform(
				                            At + FVector2D(-LabelSize.X * .5f, 9.f))), Label, ExitFont,
			                            ESlateDrawEffect::None, Color);
		}
	}

	// 小队标点：投影到屏幕画菱形和“名字 · 距离”，屏幕外的贴边显示，最后 1 秒淡出。
	const FVector2D Screen = Geometry.GetLocalSize();
	for (const auto& Ping : PC->Pings)
	{
		const double Age = Now - Ping.At;
		if (Age < 0 || Age >= AExtractionPlayerController::PingSeconds) continue;
		FVector2D At;
		const bool bInFront = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
			const_cast<AExtractionPlayerController*>(PC), Ping.Location, At, false);
		if (!bInFront)
		{
			// 在身后：按水平方向贴到屏幕底边左右两侧。
			const float Side = FRotator::NormalizeAxis((Ping.Location - ViewEye).Rotation().Yaw - ViewYaw) > 0 ? 1.f : -1.f;
			At = FVector2D(Side > 0 ? Screen.X - 40.f : 40.f, Screen.Y - 120.f);
		}
		At.X = FMath::Clamp(At.X, 40.f, Screen.X - 40.f);
		At.Y = FMath::Clamp(At.Y, 60.f, Screen.Y - 120.f);
		const float Fade = FMath::Clamp(float(AExtractionPlayerController::PingSeconds - Age), 0.f, 1.f);
		const FLinearColor Color = (Ping.bSelf ? ExtractionUIStyle::Accent() : ExtractionUIStyle::Hex(TEXT("4FC3E8")))
			.CopyWithNewOpacity(Fade);
		constexpr float R = 9.f;
		Lines({At + FVector2D(0, -R), At + FVector2D(R, 0), At + FVector2D(0, R), At + FVector2D(-R, 0), At + FVector2D(0, -R)},
		      Color, 2.5f);
		Lines({At + FVector2D(0, R), At + FVector2D(0, R + 10.f)}, Color, 2.f);
		const FString Label = FString::Printf(TEXT("%s · %.0fm"), *Ping.Sender, FVector::Dist(Ping.Location, ViewEye) / 100);
		const FSlateFontInfo PingFont = ExtractionUIStyle::Font(13, true);
		const FVector2D LabelSize = Measure->Measure(Label, PingFont);
		FSlateDrawElement::MakeText(Out, LayerId, Geometry.ToPaintGeometry(LabelSize, FSlateLayoutTransform(
			                            At + FVector2D(-LabelSize.X * .5f, -R - LabelSize.Y - 4.f))), Label, PingFont,
		                            ESlateDrawEffect::None, Color);
	}

	// 阵亡回顾：本人阵亡后在屏幕上方居中显示一张卡片，写明击杀来源、武器、距离与本局受伤情况。
	const auto* PS = PC->GetPlayerState<AExtractionPlayerState>();
	if (PS && PS->Life == EExtractionLife::Dead && PC->DeathRecapAt >= 0)
	{
		const auto& Recap = PC->DeathRecap;
		TArray<TPair<FString, FSlateFontInfo>> RecapLines;
		RecapLines.Add({TEXT("你已阵亡"), ExtractionUIStyle::Font(24, true)});
		if (Recap.Hits == 0) RecapLines.Add({TEXT("没有记录到受击来源"), ExtractionUIStyle::Font(15)});
		else
		{
			RecapLines.Add({(Recap.bBledOut ? TEXT("倒地后失血过多 · 最后击倒你的是 ") : TEXT("击杀者 ")) + Recap.Killer,
			           ExtractionUIStyle::Font(17, true)});
			RecapLines.Add({FString::Printf(TEXT("%s  ·  %.0f 米"), *Recap.Weapon, Recap.Distance), ExtractionUIStyle::Font(15)});
			RecapLines.Add({FString::Printf(TEXT("本局受击 %d 次 · 共 %.0f 点伤害"), Recap.Hits, Recap.Damage),
			           ExtractionUIStyle::Font(14)});
		}
		float Width = 0, Height = 0;
		TArray<FVector2D> Sizes;
		for (const auto& [Text, LineFont] : RecapLines)
		{
			Sizes.Add(Measure->Measure(Text, LineFont));
			Width = FMath::Max(Width, Sizes.Last().X);
			Height += Sizes.Last().Y + 6.f;
		}
		const FVector2D CardSize(Width + 64.f, Height + 36.f);
		const FVector2D CardAt(Center.X - CardSize.X * .5f, Geometry.GetLocalSize().Y * .16f);
		static const FSlateBrush Card = ExtractionUIStyle::Panel(FLinearColor::White, 6.f, FLinearColor(1, 1, 1, .1f));
		FSlateDrawElement::MakeBox(Out, LayerId, Geometry.ToPaintGeometry(CardSize, FSlateLayoutTransform(CardAt)), &Card,
		                           ESlateDrawEffect::None, ExtractionUIStyle::Hex(TEXT("0B0E12"), .86f));
		static const FSlateBrush Stripe = ExtractionUIStyle::Panel(FLinearColor::White, 1.f);
		FSlateDrawElement::MakeBox(Out, LayerId + 1, Geometry.ToPaintGeometry(FVector2D(CardSize.X, 3),
			                           FSlateLayoutTransform(CardAt)), &Stripe, ESlateDrawEffect::None,
		                           ExtractionUIStyle::Hex(TEXT("EA5A50")));
		float LineY = CardAt.Y + 20.f;
		for (int32 Index = 0; Index < RecapLines.Num(); ++Index)
		{
			const FLinearColor Color = Index == 0 ? ExtractionUIStyle::Hex(TEXT("EA5A50"))
				                           : Index == 1 ? FLinearColor::White : ExtractionUIStyle::Muted();
			FSlateDrawElement::MakeText(Out, LayerId + 1, Geometry.ToPaintGeometry(Sizes[Index], FSlateLayoutTransform(
				                            FVector2D(Center.X - Sizes[Index].X * .5f, LineY))), RecapLines[Index].Key,
			                            RecapLines[Index].Value, ESlateDrawEffect::None, Color);
			LineY += Sizes[Index].Y + 6.f;
		}
		++LayerId;
	}
	return LayerId;
}

// 请求服务器选择上一位合法观战队友。
void UExtractionWidget::PreviousClicked()
{
	if (auto* PC = GetOwningPlayer<AExtractionPlayerController>()) PC->ServerCycleSpectator(-1);
}

// 请求服务器选择下一位合法观战队友。
void UExtractionWidget::NextClicked()
{
	if (auto* PC = GetOwningPlayer<AExtractionPlayerController>()) PC->ServerCycleSpectator(1);
}

// 按钮只提交离开请求，会话清理由服务器确认后的客户端链路处理。
void UExtractionWidget::LeaveClicked()
{
	if (auto* PC = GetOwningPlayer<AExtractionPlayerController>()) PC->ServerLeaveExtraction();
}
