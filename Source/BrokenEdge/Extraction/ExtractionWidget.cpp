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

void UExtractionWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
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
		RaidInfo->SetText(FText::FromString(FString::Printf(TEXT("航天基地  ·  存活 %d"), GS->AlivePlayers)));
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
		Text += Result.Outcome == EExtractionOutcome::Extracted ? TEXT("\n已带出：") : TEXT("\n本次物资：");
		for (FName Id : UExtractionItemDefinition::Ids())
		{
			int32 Count = 0;
			for (const auto& I : Result.Items) if (I.Definition == Id) Count += I.Quantity;
			if (Count)
				Text += FString::Printf(TEXT("\n%s × %d"), *UExtractionItemDefinition::Get(Id)->Label.ToString(),
				                        Count);
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
			Detail = FString::Printf(TEXT("/ %d\n%s"), ControlledCharacter->GetCombat()->GetCarriedAmmo(),
			                         Ammo ? *Ammo->Label.ToString() : TEXT("弹药"));
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
