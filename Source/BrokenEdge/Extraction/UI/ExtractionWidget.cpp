#include "BrokenEdge/Extraction/UI/ExtractionWidget.h"
#include "BrokenEdge/Extraction/UI/ExtractionHUDMetrics.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("ExtractionHUD"), STATGROUP_ExtractionHUD, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("Refresh"), STAT_ExtractionHUDRefresh, STATGROUP_ExtractionHUD);
#include "BrokenEdge/Extraction/World/ExtractionDoor.h"
#include "BrokenEdge/Extraction/Map/ExtractionMapDefinition.h"
#include "Engine/Texture2D.h"
#include "BrokenEdge/Extraction/UI/ExtractionUIStyle.h"
#include "BrokenEdge/Extraction/Core/ExtractionPlayerController.h"
#include "BrokenEdge/Traversal/ExtractionTraversalComponent.h"
#include "BrokenEdge/Extraction/Map/ExtractionRegionStream.h"
#include "BrokenEdge/Extraction/Health/ExtractionHealth.h"
#include "BrokenEdge/Extraction/Combat/ExtractionMeleeComponent.h"
#include "BrokenEdge/Extraction/Core/ExtractionInteractionComponent.h"
#include "BrokenEdge/Extraction/Combat/ExtractionAbilities.h"
#include "BrokenEdge/Extraction/Combat/ExtractionSmoke.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionInventoryComponent.h"
#include "BrokenEdge/Extraction/Core/ExtractionAccountComponent.h"
#include "BrokenEdge/Extraction/World/ExtractionExit.h"
#include "BrokenEdge/Extraction/World/ExtractionExitRegistry.h"
#include "BrokenEdge/Extraction/World/ExtractionExitSwitch.h"
#include "EngineUtils.h"
#include "BrokenEdge/Extraction/Core/ExtractionState.h"
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
#include "Components/CapsuleComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Styling/CoreStyle.h"

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
    TRACE_CPUPROFILER_EVENT_SCOPE(ExtractionHUDRefresh);
    SCOPE_CYCLE_COUNTER(STAT_ExtractionHUDRefresh);
    const ExtractionHUDMetrics::FRefreshScope ProfileScope;
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
	ExtractionHUDMetrics::SetText(Compass, FText::FromString(FString::Printf(TEXT("%s  %03d°"), Directions[((Heading + 22) / 45) % 8], Heading)));
	// 只给"在烟里"的信号太生硬，改为按距烟心的深度渐变：烟心最浓 .55，边缘接近 0。
	const float Haze = AExtractionSmoke::Haze(GetWorld(), Eye);
	SmokeHaze->SetVisibility(Haze > .01f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	SmokeHaze->SetBrushColor(FLinearColor(.34f, .34f, .34f, Haze * .55f));
	const int32 Seconds = FMath::Max(0, FMath::CeilToInt(GS->Deadline - GS->GetServerWorldTimeSeconds()));
	FString Text;
	const FString MapName = GS->MapDefinition ? GS->MapDefinition->DisplayName.ToString() : TEXT("地图加载中");
	// 按键提示放进右侧中间的卡片；旧布局没有该卡片时仍并入整段文本。
	FString Hints;
	FString& HintText = KeyHints ? Hints : Text;
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
        if (KeyHints)
        {
            if (CachedHintLife != int32(PS->Life))
            {
                CachedHintLife = int32(PS->Life);
                CachedKeyHints = FString(Hint) + TEXT("\nTab 背包 · M 地图") + TEXT("\nE 搜索/拾取 · F 开门/拉闸")
				+ TEXT("\nR 换弹 · 1/2 主副武器 · 3 刀") + TEXT("\nCtrl 蹲下 · Z 趴下 · Shift 冲刺")
				+ TEXT("\nH 治疗 · G 手雷 · 中键 标点");
            }
            HintText = CachedKeyHints;
        }
		else Text = RaidTimer
			       ? FString(Hint)
			       : FString::Printf(TEXT("%s · 小队行动\n剩余 %02d:%02d · 存活 %d\n%s"), *MapName, Seconds / 60, Seconds % 60,
			                         GS->AlivePlayers, Hint);
	}
	if (RaidTimer)
	{
		const bool bActive = GS->Phase == EExtractionPhase::Active;
		ExtractionHUDMetrics::SetText(RaidTimer, FText::FromString(bActive ? FString::Printf(TEXT("%02d:%02d"), Seconds / 60, Seconds % 60)
			                                     : TEXT("--:--")));
		// 最后五分钟转红，提醒尽快撤离。
		RaidTimer->SetColorAndOpacity(FSlateColor(bActive && Seconds <= 300
			                                          ? ExtractionUIStyle::Hex(TEXT("EA5A50"))
			                                          : FLinearColor::White));
	}
	if (RaidInfo)
		ExtractionHUDMetrics::SetText(RaidInfo, FText::FromString(FString::Printf(TEXT("%s  ·  存活 %d  ·  击杀守卫 %d"), *MapName,
		                                                    GS->AlivePlayers, PS->RaidProgress.GuardKills)));
	if (!GS->StorageError.IsEmpty()) Text = GS->StorageError;
	if (PC->ExtractionAccount && PS->Life == EExtractionLife::Alive)
	{
        const auto Key = MakeTuple(PC->ExtractionAccount->Profile.Profile, PC->ExtractionAccount->Profile.Revision, PS->RaidProgress.GuardKills);
        if (!bQuestCacheValid || QuestKey != Key)
        {
            QuestKey = Key; bQuestCacheValid = true; CachedQuestText.Reset();
		const auto& Progression = PC->ExtractionAccount->Profile.Progression;
		const auto* Rules = GetDefault<UExtractionProgressionRules>();
        for (const auto& Quest : Progression.Quests)
        {
            const auto* Definition = Rules->Quest(Quest.QuestId);
            if (!Definition || !Quest.Accepted || Quest.Claimed) continue;
            CachedQuestText += TEXT("\n任务 · ") + Definition->Label;
            if (Definition->Type == EQuestObjective::KillGuards)
                CachedQuestText += FString::Printf(TEXT(" %d / %d（本局 +%d，撤离保存）"), Quest.Progress,
                    Definition->Target, PS->RaidProgress.GuardKills);
            else CachedQuestText += Definition->Type == EQuestObjective::Extract ? TEXT(" · 成功撤离后推进") : TEXT(" · 文件带出后回大厅提交");
        }
        }
        Text += CachedQuestText;
	}
	FString ExitsText;
	FString CountdownText;
	float ExitFraction = 0;
	bool bShowExitProgress = false;
	if (PS->Life == EExtractionLife::Alive && GS->Phase == EExtractionPhase::Active && PC->GetPawn())
	{
		for (const auto& Reference : GetWorld()->GetSubsystem<UExtractionExitRegistry>()->GetExits())
		{
			const auto* It = Reference.Get();
			if (!IsValid(It)) continue;
			const FString Reason = It->UnavailableReason(GS->GetServerWorldTimeSeconds());
			FString Rule;
			if (It->Definition.bRequireEmptyPack) Rule = TEXT(" · 需丢掉背包");
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
	ExtractionHUDMetrics::SetText(ExitStatus, FText::FromString(ExitsText));
	ExtractionHUDMetrics::SetText(ExtractionCountdown, FText::FromString(CountdownText));
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
		if (!TraversalPrompt.IsEmpty()) HintText += TEXT("\n") + TraversalPrompt;
		const auto* A = C->GetExtractionAbilities();
		if (PS->Life == EExtractionLife::Alive)
		{
			const double HealIn = FMath::Max(0.0, A->HealReadyAt - Now), SmokeIn = FMath::Max(0.0, A->SmokeReadyAt - Now);
			HintText += HealIn > 0 ? FString::Printf(TEXT("\nV 激素枪 · %.0fs"), HealIn)
				            : A->bStimDrawn
				            ? FString(TEXT("\n左键 射激素飞镖给队友 · V 收起"))
				            : FString(TEXT("\nV 切出激素枪 · 长按 V 给自己打针"));
			HintText += SmokeIn > 0 ? FString::Printf(TEXT("\nX 蜂巢烟雾 · %.0fs"), SmokeIn)
				            : FString(TEXT("\nX 蜂巢烟雾（Alt+X 低抛）"));
			if (C->GetExtractionHealth()->Painkiller) Text += TEXT("\n激素生效 · 止痛中，骨折暂不影响移动");
			if (A->RescueFocus()) HintText += TEXT("\n按住 E 救援队友 · 5 秒");
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
	if (const auto* C = Cast<ABrokenEdgeCharacter>(PC->GetPawn()))
		if (const auto* I = C->GetExtractionInteraction(); I && I->OrdinaryDoorFocus)
			HintText += I->OrdinaryDoorFocus->bOpen ? TEXT("\nF 关门") : TEXT("\nF 开门");
	Text.TrimStartAndEndInline();
	ExtractionHUDMetrics::SetText(Status, FText::FromString(Text));
	if (KeyHints)
	{
		Hints.TrimStartAndEndInline();
		ExtractionHUDMetrics::SetText(KeyHints, FText::FromString(Hints));
		// 文本外面是卡片的竖排盒和边框，提示为空时整张卡片收起。
		if (UWidget* Card = KeyHints->GetParent() ? KeyHints->GetParent()->GetParent() : nullptr)
			Card->SetVisibility(Hints.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	UpdateMinimap(PS->Life == EExtractionLife::Alive || PS->Life == EExtractionLife::Downed ? PC->GetPawn() : nullptr);
    UpdateDiscreteStatus(Cast<ABrokenEdgeCharacter>(PC->GetPawn()));
	Previous->SetVisibility(PC->bExtractionSpectating ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	Next->SetVisibility(Previous->GetVisibility());
	Previous->SetIsEnabled(PC->SpectatedPlayer != nullptr);
	Next->SetIsEnabled(PC->SpectatedPlayer != nullptr);
	Leave->SetVisibility(PC->IsMenuOpen() || PC->bExtractionSpectating || GS->Phase == EExtractionPhase::Ended
		                     ? ESlateVisibility::Visible
		                     : ESlateVisibility::Collapsed);
}

void UExtractionWidget::UnbindStatusEvents()
{
    if (ObservedCharacter.IsValid()) ObservedCharacter->OnVitalsChanged.Remove(VitalsHandle);
    if (ObservedInventory.IsValid()) ObservedInventory->OnContentsChanged.Remove(InventoryHandle);
    ObservedCharacter.Reset(); ObservedInventory.Reset(); ObservedWeapon.Reset();
    VitalsHandle.Reset(); InventoryHandle.Reset(); bStatusDirty = true;
}

void UExtractionWidget::UpdateDiscreteStatus(ABrokenEdgeCharacter* ControlledCharacter)
{
    auto* Inventory = ControlledCharacter ? ControlledCharacter->GetExtractionInteraction()->Inventory() : nullptr;
    if (ObservedCharacter.Get() != ControlledCharacter || ObservedInventory.Get() != Inventory)
    {
        UnbindStatusEvents();
        ObservedCharacter = ControlledCharacter; ObservedInventory = Inventory;
        if (ControlledCharacter) VitalsHandle = ControlledCharacter->OnVitalsChanged.AddWeakLambda(this,
            [this](const FExtractionVitalsChange&) { bStatusDirty = true; });
        if (Inventory) InventoryHandle = Inventory->OnContentsChanged.AddWeakLambda(this, [this] { bStatusDirty = true; });
    }
    auto* Weapon = ControlledCharacter ? ControlledCharacter->GetEquippedWeapon() : nullptr;
    const auto* Trauma = ControlledCharacter ? ControlledCharacter->GetExtractionHealth() : nullptr;
    const auto Key = MakeTuple(ControlledCharacter ? ControlledCharacter->GetHealth() : 0.f,
        ControlledCharacter ? ControlledCharacter->GetMaxHealth() : 0.f,
        Weapon ? Weapon->GetAmmo() : 0, ControlledCharacter ? ControlledCharacter->GetCombat()->GetCarriedAmmo() : 0,
        ControlledCharacter && ControlledCharacter->Melee->Active, Trauma && Trauma->Bleeding, Trauma && Trauma->LegFracture,
        Weapon ? Weapon->ExtractionDefinition : NAME_None, Weapon ? Weapon->ExtractionAmmoType : NAME_None,
        Weapon ? Weapon->GetMagCapacity() : 0, Inventory ? Inventory->GetContents().Revision : INDEX_NONE);
    // The cheap key also catches client prediction, replicated HP/trauma, and pawn
    // changes. These must not depend on an authority-only damage event arriving.
    if (!bStatusDirty && ObservedWeapon.Get() == Weapon && StatusKey == Key) return;
    StatusKey = Key; ObservedWeapon = Weapon; bStatusDirty = false;
	FString VitalText;
	FString WeaponText;
	HealthProgress->SetVisibility(ControlledCharacter ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (ControlledCharacter)
	{
		const float HealthFraction = ControlledCharacter->GetHealth() / FMath::Max(1.f, ControlledCharacter->GetMaxHealth());
		HealthProgress->SetPercent(HealthFraction);
		// 生命条按剩余比例由绿转黄再转红。
		HealthProgress->SetFillColorAndOpacity(HealthFraction > .6f
			                                       ? ExtractionUIStyle::Hex(TEXT("5DBB72"))
			                                       : HealthFraction > .3f
			                                       ? ExtractionUIStyle::Accent()
			                                       : ExtractionUIStyle::Hex(TEXT("EA5A50")));
		if (HealthText) ExtractionHUDMetrics::SetText(HealthText, FText::AsNumber(FMath::CeilToInt(ControlledCharacter->GetHealth())));
		else
			VitalText = FString::Printf(TEXT("生命 %d / %d\n"),
			                            FMath::CeilToInt(ControlledCharacter->GetHealth()),
			                            FMath::CeilToInt(ControlledCharacter->GetMaxHealth()));
		if (const auto* Inv = ControlledCharacter->GetExtractionInteraction()->Inventory())
		{
			for (int32 ArmorSlot = 2; ArmorSlot <= 3; ++ArmorSlot)
			{
				const auto* Armor = Inv->GetContents().InSlot(ArmorSlot);
				VitalText += (ArmorSlot == 2 ? TEXT("头盔 ") : TEXT("  胸甲 ")) + (Armor
					                                                               ? ExtractionItems::Details(*Armor)
					                                                               : FString(TEXT("无")));
			}
			VitalText += TEXT("\n");
		}
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
		else if (Weapon)
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
			ExtractionHUDMetrics::SetText(WeaponName, FText::FromString(Name));
			ExtractionHUDMetrics::SetText(AmmoText, FText::FromString(Rounds));
			AmmoText->SetColorAndOpacity(FSlateColor(bLowAmmo ? ExtractionUIStyle::Hex(TEXT("EA5A50")) : FLinearColor::White));
			AmmoText->SetVisibility(Rounds.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
			WeaponText = Detail;
		}
		else WeaponText = Name + TEXT("\n") + (Rounds.IsEmpty() ? Detail : Rounds + TEXT(" ") + Detail);
    }
    else
    {
        if (HealthText) ExtractionHUDMetrics::SetText(HealthText, FText::FromString(TEXT("--")));
        if (WeaponName) ExtractionHUDMetrics::SetText(WeaponName, FText::GetEmpty());
        if (AmmoText) { ExtractionHUDMetrics::SetText(AmmoText, FText::GetEmpty()); AmmoText->SetVisibility(ESlateVisibility::Collapsed); }
        HealthProgress->SetPercent(0.f);
    }
    ExtractionHUDMetrics::SetText(Vitals, FText::FromString(VitalText));
	ExtractionHUDMetrics::SetText(WeaponStatus, FText::FromString(WeaponText));
}

int32 UExtractionWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
                                     FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style,
                                     bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, Geometry, Culling, Out, LayerId, Style, bParentEnabled) + 1;
	const auto* PC = GetOwningPlayer<AExtractionPlayerController>();
	if (!PC || !GetWorld()) return LayerId;
	PaintMinimap(Geometry, Out, LayerId);
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
			                           : Text.StartsWith(TEXT("队友"))
			                           ? ExtractionUIStyle::Hex(TEXT("4FC3E8"))
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

	// 方位刻度条：指南针卡片下方，视线左右各 60°，每 5° 一小格、15° 一中格、45° 标方位字；
	// 撤离点（绿/灰）与小队标点（琥珀/青）按方位画在刻度条上，超出视野的贴在两端。
	{
		constexpr float HalfSpan = 60.f, StripWidth = 480.f;
		const float StripY = 72.f;
		const float Left = Center.X - StripWidth * .5f;
		auto ToX = [&](float Bearing, bool bClamp) -> float
		{
			const float Delta = FRotator::NormalizeAxis(Bearing - ViewYaw);
			if (!bClamp && FMath::Abs(Delta) > HalfSpan) return -1.f;
			return float(Center.X + FMath::Clamp(Delta, -HalfSpan, HalfSpan) / HalfSpan * StripWidth * .5f);
		};
		Lines({FVector2D(Left, StripY), FVector2D(Left + StripWidth, StripY)}, FLinearColor(1, 1, 1, .25f), 1.f);
		const FSlateFontInfo TickFont = ExtractionUIStyle::Font(11, true);
		static const TCHAR* Names[] = {TEXT("N"), TEXT("NE"), TEXT("E"), TEXT("SE"), TEXT("S"), TEXT("SW"), TEXT("W"), TEXT("NW")};
		for (int32 Degree = 0; Degree < 360; Degree += 5)
		{
			const float X = ToX(Degree, false);
			if (X < 0) continue;
			const bool bMajor = Degree % 45 == 0, bMid = Degree % 15 == 0;
			const float Height = bMajor ? 10.f : bMid ? 6.f : 3.f;
			// 越靠两端越淡，读起来不会太杂。
			const float Edge = 1.f - FMath::Abs(X - Center.X) / (StripWidth * .5f);
			Lines({FVector2D(X, StripY), FVector2D(X, StripY + Height)}, FLinearColor(1, 1, 1, .2f + .6f * Edge), 1.f);
			if (bMajor)
			{
				const FString Name = Names[Degree / 45];
				const FVector2D NameSize = Measure->Measure(Name, TickFont);
				FSlateDrawElement::MakeText(Out, LayerId, Geometry.ToPaintGeometry(NameSize, FSlateLayoutTransform(
					                            FVector2D(X - NameSize.X * .5f, StripY + 12.f))), Name, TickFont,
				                            ESlateDrawEffect::None,
				                            (Degree == 0 ? ExtractionUIStyle::Accent() : FLinearColor::White).CopyWithNewOpacity(.3f + .7f * Edge));
			}
		}
		// 视线正中的小三角。
		Lines({FVector2D(Center.X - 5, StripY - 7), FVector2D(Center.X + 5, StripY - 7), FVector2D(Center.X, StripY - 1),
		       FVector2D(Center.X - 5, StripY - 7)}, ExtractionUIStyle::Accent(), 1.5f);
		auto Pin = [&](const FVector& World, FLinearColor Color)
		{
			const float X = ToX((World - ViewEye).Rotation().Yaw, true);
			Lines({FVector2D(X - 4, StripY - 4), FVector2D(X, StripY - 9), FVector2D(X + 4, StripY - 4), FVector2D(X, StripY + 1),
			       FVector2D(X - 4, StripY - 4)}, Color, 2.f);
		};
		if (const auto* State = GetWorld()->GetGameState<AExtractionGameState>())
			for (const auto& Reference : GetWorld()->GetSubsystem<UExtractionExitRegistry>()->GetExits())
				if (const auto* Exit = Reference.Get(); IsValid(Exit))
				Pin(Exit->GetActorLocation(), Exit->UnavailableReason(State->GetServerWorldTimeSeconds()).IsEmpty()
					                            ? ExtractionUIStyle::Hex(TEXT("5DBB72"))
					                            : FLinearColor(.7f, .72f, .75f, .6f));
		for (const auto& Ping : PC->Pings)
			if (Now - Ping.At < AExtractionPlayerController::PingSeconds)
				Pin(Ping.Location, Ping.bSelf ? ExtractionUIStyle::Accent() : ExtractionUIStyle::Hex(TEXT("4FC3E8")));
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

	// 拉闸点标记：撤离点本身不标注；本局启用的电闸投影到屏幕，待拉为琥珀色、已拉为淡绿色；屏幕外贴边。
	const auto* GS = GetWorld()->GetGameState<AExtractionGameState>();
	const auto* Me = PC->GetPlayerState<AExtractionPlayerState>();
	if (GS && Me && Me->Life == EExtractionLife::Alive && GS->Phase == EExtractionPhase::Active && !PC->bExtractionSpectating)
	{
		const FVector2D ScreenSize = Geometry.GetLocalSize();
		const FSlateFontInfo ExitFont = ExtractionUIStyle::Font(12, true);
		for (TActorIterator<AExtractionExitSwitch> It(GetWorld()); It; ++It)
		{
			if (!It->bActive) continue;
			const bool bPulled = It->bPulled;
			const FVector Location = It->GetActorLocation() + FVector(0, 0, 160);
			FVector2D At;
			if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
				const_cast<AExtractionPlayerController*>(PC), Location, At, false))
			{
				const float Side = FRotator::NormalizeAxis((Location - ViewEye).Rotation().Yaw - ViewYaw) > 0 ? 1.f : -1.f;
				At = FVector2D(Side > 0 ? ScreenSize.X - 40.f : 40.f, ScreenSize.Y * .5f);
			}
			At.X = FMath::Clamp(At.X, 40.f, ScreenSize.X - 40.f);
			At.Y = FMath::Clamp(At.Y, 80.f, ScreenSize.Y - 140.f);
			const FLinearColor Color = bPulled ? ExtractionUIStyle::Hex(TEXT("5DBB72"), .55f) : ExtractionUIStyle::Hex(TEXT("F2A93B"), .95f);
			// 闪电形小标记，下方写“拉闸点 · 距离”；吊桥的锁标“吊桥电闸”。
			Lines({At + FVector2D(2, -9), At + FVector2D(-5, 1), At + FVector2D(2, 1), At + FVector2D(-2, 9)}, Color, 2.f);
			const TCHAR* Name = It->Bridge ? TEXT("吊桥电闸") : It->Water ? TEXT("注水开关") : TEXT("拉闸点");
			const FString Label = FString::Printf(TEXT("%s%s · %.0fm"), Name, bPulled ? TEXT("（已拉）") : TEXT(""),
			                                      FVector::Dist(It->GetActorLocation(), ViewEye) / 100);
			const FVector2D LabelSize = Measure->Measure(Label, ExitFont);
			FSlateDrawElement::MakeText(Out, LayerId, Geometry.ToPaintGeometry(LabelSize, FSlateLayoutTransform(
				                            At + FVector2D(-LabelSize.X * .5f, 11.f))), Label, ExitFont,
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

void UExtractionWidget::NativeDestruct()
{
    UnbindStatusEvents(); bQuestCacheValid = false; CachedHintLife = INDEX_NONE;
	MiniOverview = nullptr;
	MiniDefinition = nullptr;
	Super::NativeDestruct();
}

// 小地图底图来自离线烘焙的整图俯视地图（Scripts/Extraction/bake_overview_map.py），不再每帧采集场景。
void UExtractionWidget::UpdateMinimap(const APawn* Pawn)
{
	bMiniActive = Pawn != nullptr;
	if (!Pawn) return;
	const auto* GS = GetWorld()->GetGameState<AExtractionGameState>();
	auto* Map = GS ? GS->MapDefinition.Get() : nullptr;
	if (MiniDefinition != Map)
	{
		MiniDefinition = Map;
		MiniOverview = Map ? Map->OverviewTexture.LoadSynchronous() : nullptr;
		MiniMin = Map ? Map->OverviewTextureMin : FVector2D::ZeroVector;
		MiniMax = Map ? Map->OverviewTextureMax : FVector2D::ZeroVector;
	}
	MiniCenter = Pawn->GetActorLocation();
}

void UExtractionWidget::PaintMinimap(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const
{
	const auto* PC = GetOwningPlayer<AExtractionPlayerController>();
	if (!bMiniActive || !PC || !PC->GetPawn()) return;
	const FVector2D At(24, 24), Size(MiniSize, MiniSize);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	auto Box = [&](FVector2D P, FVector2D S, FLinearColor C, int32 Z)
	{
		FSlateDrawElement::MakeBox(Out, LayerId + Z, Geometry.ToPaintGeometry(S, FSlateLayoutTransform(P)), White,
		                           ESlateDrawEffect::None, C);
	};
	Box(At - FVector2D(3), Size + FVector2D(6), ExtractionUIStyle::Hex(TEXT("0B0E12"), .85f), 0);
	// 截取俯视图：视野内的世界范围与俯视图范围求交，只画相交的那一块（地图边缘外留空）。
	if (MiniOverview && MiniMax.X > MiniMin.X && MiniMax.Y > MiniMin.Y)
	{
		const double Half = MiniWorld * .5;
		// 屏幕左右 = 世界 Y，屏幕上下 = 世界 -X。
		const double Left = FMath::Max(MiniCenter.Y - Half, MiniMin.Y), Right = FMath::Min(MiniCenter.Y + Half, MiniMax.Y);
		const double TopX = FMath::Min(MiniCenter.X + Half, MiniMax.X), BottomX = FMath::Max(MiniCenter.X - Half, MiniMin.X);
		if (Right > Left && TopX > BottomX)
		{
			const FVector2D Span = MiniMax - MiniMin;
			FSlateBrush Brush;
			Brush.SetResourceObject(MiniOverview);
			Brush.SetUVRegion(FBox2f(
				FVector2f((Left - MiniMin.Y) / Span.Y, (MiniMax.X - TopX) / Span.X),
				FVector2f((Right - MiniMin.Y) / Span.Y, (MiniMax.X - BottomX) / Span.X)));
			const float PerCm = MiniSize / MiniWorld;
			const FVector2D Offset((Left - (MiniCenter.Y - Half)) * PerCm, ((MiniCenter.X + Half) - TopX) * PerCm);
			const FVector2D Draw((Right - Left) * PerCm, (TopX - BottomX) * PerCm);
			Brush.ImageSize = Draw;
			FSlateDrawElement::MakeBox(Out, LayerId + 1, Geometry.ToPaintGeometry(Draw, FSlateLayoutTransform(At + Offset)),
			                           &Brush, ESlateDrawEffect::None, FLinearColor(1, 1, 1, .92f));
		}
	}
	const FLinearColor Edge(1, 1, 1, .12f);
	Box(At, {Size.X, 1}, Edge, 2);
	Box(At + FVector2D(0, Size.Y - 1), {Size.X, 1}, Edge, 2);
	Box(At, {1, Size.Y}, Edge, 2);
	Box(At + FVector2D(Size.X - 1, 0), {1, Size.Y}, Edge, 2);
	const FVector2D Mid = At + Size * .5f;
	const float PerCm = MiniSize / MiniWorld;
	// 世界 → 小地图：X 朝上、Y 朝右；超出边框的标记不画。
	auto Project = [&](const FVector& World, FVector2D& P)
	{
		const FVector D = World - MiniCenter;
		P = Mid + FVector2D(D.Y, -D.X) * PerCm;
		return P.X >= At.X + 4 && P.Y >= At.Y + 4 && P.X <= At.X + Size.X - 4 && P.Y <= At.Y + Size.Y - 4;
	};
	auto Label = [&](FVector2D P, const FString& Value, FLinearColor C)
	{
		FSlateDrawElement::MakeText(Out, LayerId + 4, Geometry.ToPaintGeometry(FVector2D(60, 16), FSlateLayoutTransform(P)),
		                            Value, ExtractionUIStyle::Font(10), ESlateDrawEffect::None, C);
	};
	FVector2D P;
	// 本局启用的电闸：琥珀待拉、绿色已拉。
	for (TActorIterator<AExtractionExitSwitch> It(GetWorld()); It; ++It)
		if (It->bActive && Project(It->GetActorLocation(), P))
		{
			const FLinearColor C = It->bPulled ? FLinearColor(.25f, .9f, .4f) : ExtractionUIStyle::Accent();
			Box(P - FVector2D(3.5f), {7, 7}, C, 3);
			Label(P + FVector2D(5, -8), It->Water ? TEXT("水") : TEXT("闸"), C);
		}
	// 小队标点。
	for (const auto& Ping : PC->Pings)
		if (GetWorld()->GetTimeSeconds() - Ping.At < AExtractionPlayerController::PingSeconds && Project(Ping.Location, P))
		{
			const FLinearColor C = Ping.bSelf ? ExtractionUIStyle::Accent() : ExtractionUIStyle::Hex(TEXT("4FC3E8"));
			Box(P - FVector2D(.5f, 6), {1, 12}, C, 3);
			Box(P - FVector2D(6, .5f), {12, 1}, C, 3);
		}
	// 队友：青色圆点，带楼层高差。
	const auto* Local = PC->GetPlayerState<AExtractionPlayerState>();
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		const auto* Mate = It->GetPlayerState<AExtractionPlayerState>();
		if (!Mate || !Local || Mate == Local || Local->ExtractionSquad <= 0 || Mate->ExtractionSquad != Local->ExtractionSquad
			|| !Project(It->GetActorLocation(), P))
			continue;
		const FLinearColor C = ExtractionUIStyle::Hex(TEXT("4FC3E8"));
		Box(P - FVector2D(3.5f), {7, 7}, C, 3);
		const float Rise = (It->GetActorLocation().Z - PC->GetPawn()->GetActorLocation().Z) / 100.f;
		if (FMath::Abs(Rise) >= 2.f) Label(P + FVector2D(5, -8), FString::Printf(TEXT("%+.0fm"), Rise), C);
	}
	// 自己：中心的朝向箭头。偏航 0 朝 +X，即画面上方。
	const float Yaw = FMath::DegreesToRadians(PC->GetControlRotation().Yaw);
	const FVector2D Fwd(FMath::Sin(Yaw), -FMath::Cos(Yaw)), Right(-Fwd.Y, Fwd.X);
	const TArray<FVector2D> Arrow = {Mid + Fwd * 9.f, Mid - Fwd * 5.f + Right * 6.f, Mid - Fwd * 2.f, Mid - Fwd * 5.f - Right * 6.f,
	                                 Mid + Fwd * 9.f};
	FSlateDrawElement::MakeLines(Out, LayerId + 5, Geometry.ToPaintGeometry(), Arrow, ESlateDrawEffect::None,
	                             FLinearColor::White, true, 2.f);
	Label(FVector2D(Mid.X - 4, At.Y + 2), TEXT("N"), FLinearColor(1, 1, 1, .7f));
	Label(At + FVector2D(6, Size.Y - 18), FString::Printf(TEXT("%.0fm"), MiniWorld / 100.f), FLinearColor(1, 1, 1, .5f));
}
