#include "ExtractionPlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "ExtractionDoor.h"
#include "ExtractionWidget.h"
#include "ExtractionTacticalMap.h"
#include "BrokenEdge/Traversal/ExtractionTraversalComponent.h"
#include "ExtractionRegionStream.h"
#include "ExtractionHealth.h"
#include "ExtractionMeleeComponent.h"
#include "ExtractionInteractionComponent.h"
#include "ExtractionKeyRoom.h"
#include "ExtractionExitSwitch.h"
#include "ExtractionZipline.h"
#include "ExtractionFragGrenade.h"
#include "ExtractionAbilities.h"
#include "ExtractionSmoke.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionLootActor.h"
#include "LootingMainView.h"
#include "ExtractionAccountComponent.h"
#include "ExtractionExit.h"
#include "EngineUtils.h"
#include "ExtractionState.h"
#include "ExtractionGameMode.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "Camera/CameraActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "MultiplayerSessionsSubsystem.h"
#include "TimerManager.h"
// 撤离控制器把本地输入转为服务器 RPC，并管理 HUD、背包、观战和离开会话。
// 本地界面只读复制状态；物品结果、撤离成功和观战资格由服务器决定。HUD 实现位于 ExtractionWidget。

// 观战目标与观战标记仅同步给控制器拥有者，其他玩家不需要该私有视角状态。
void AExtractionPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AExtractionPlayerController, SpectatedPlayer, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AExtractionPlayerController, bExtractionSpectating, COND_OwnerOnly);
}

// 在父类输入之上绑定菜单、背包、地图、医疗、能力和武器槽；E 松开用于停止救援。
void AExtractionPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ThisClass::ToggleExtractionMenu);
	InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ThisClass::ToggleInventory);
	InputComponent->BindKey(EKeys::M, IE_Pressed, this, &ThisClass::ToggleTacticalMap);
	InputComponent->BindKey(EKeys::H, IE_Pressed, this, &ThisClass::UseMedical);
	// Z 趴下；激素枪改到 V。
	InputComponent->BindKey(EKeys::Z, IE_Pressed, this, &ThisClass::ToggleProne);
	InputComponent->BindKey(EKeys::V, IE_Pressed, this, &ThisClass::HealAbility);
	InputComponent->BindKey(EKeys::V, IE_Released, this, &ThisClass::HealReleased);
	InputComponent->BindKey(EKeys::X, IE_Pressed, this, &ThisClass::SmokeAbility);
	// 官方蜂巢烟雾支持低抛；组合键要求修饰键完全匹配，单按 X 不会触发这一条。
	InputComponent->BindKey(FInputChord(EKeys::X, false, false, true, false), IE_Pressed, this,
	                        &ThisClass::SmokeAbilityLow);
	InputComponent->BindKey(EKeys::G, IE_Pressed, this, &ThisClass::ThrowGrenade);
	InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Pressed, this, &ThisClass::PlacePing);
	InputComponent->BindKey(FInputChord(EKeys::G, false, false, true, false), IE_Pressed, this,
	                        &ThisClass::ThrowGrenadeLow);
	InputComponent->BindKey(EKeys::E, IE_Released, this, &ThisClass::ReleaseInteract);
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ThisClass::SelectKnife);
	InputComponent->BindKey(EKeys::One, IE_Pressed, this, &ThisClass::SelectPrimaryGun);
	InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ThisClass::SelectSecondaryGun);
}

// 菜单未打开时请求服务器选择刀槽；实际资格由近战组件检查。
void AExtractionPlayerController::SelectKnife()
{
	if (auto* C = Cast<ABrokenEdgeCharacter>(GetPawn()); C && !bMenuOpen) C->Melee->Select(true);
}

// 请求退出刀槽回到当前枪，不额外交换主副武器。
void AExtractionPlayerController::SelectHeldGun()
{
	if (auto* C = Cast<ABrokenEdgeCharacter>(GetPawn()); C && !bMenuOpen) C->Melee->Select(false);
}

// 请求退出刀槽并切换另一把枪，仍受服务器动作锁约束。
void AExtractionPlayerController::SelectOtherGun()
{
	if (auto* C = Cast<ABrokenEdgeCharacter>(GetPawn()); C && !bMenuOpen) C->Melee->Select(false, true);
}

// 1 / 2 固定对应主武器槽（槽 0）/ 副武器槽（槽 1），切枪不会交换两把枪的槽位。
void AExtractionPlayerController::SelectWeaponSlot(int32 Slot)
{
	auto* C = Cast<ABrokenEdgeCharacter>(GetPawn());
	const auto* I = Interaction();
	const auto* Inv = I ? I->Inventory() : nullptr;
	if (!C || bMenuOpen || !Inv) return;
	// 拿着激素枪时按 1/2 先收起激素枪，回到对应的枪。
	if (C->GetExtractionAbilities()->bStimDrawn) C->GetExtractionAbilities()->SetStimDrawn(false);
	const auto* Wanted = Inv->GetContents().InSlot(Slot);
	if (!Wanted)
	{
		ShowItemMessage(Slot == 0 ? TEXT("主武器槽没有武器") : TEXT("副武器槽没有武器"));
		return;
	}
	// 槽位固定，手持哪一把看 HeldSlot；已拿着这一槽就只收刀，否则切到这一槽。
	C->Melee->Select(false, Inv->HeldSlot() != Slot);
}

void AExtractionPlayerController::SelectPrimaryGun() { SelectWeaponSlot(0); }

void AExtractionPlayerController::ToggleProne()
{
	auto* I = Interaction();
	if (auto* C = Cast<ABrokenEdgeCharacter>(GetPawn()); C && !bMenuOpen && (!I || !I->InventoryOpen)) C->ToggleProne();
}
void AExtractionPlayerController::SelectSecondaryGun() { SelectWeaponSlot(1); }

// Esc 按优先级关闭地图、取消拖动、关闭背包或取消治疗，均无待处理操作时才切换对局菜单。
void AExtractionPlayerController::ToggleExtractionMenu()
{
	if (TacticalMap && TacticalMap->IsInViewport())
	{
		TacticalMap->RemoveFromParent();
		return;
	}
	if (InventoryWidget && InventoryWidget->CancelDrag()) return;
	if (auto* I = Interaction())
	{
		if (I->InventoryOpen)
		{
			RequestOpenInventory(nullptr, false);
			return;
		}
		if (I->MedicalEnd > 0)
		{
			ServerCancelMedical();
			return;
		}
	}
	bMenuOpen = !bMenuOpen;
	if (bMenuOpen) StopHeldActions();
}

// 切换只读战术地图，必要时关闭背包；HitTestInvisible 使地图不截获游戏鼠标输入。
void AExtractionPlayerController::ToggleTacticalMap()
{
	if (TacticalMap && TacticalMap->IsInViewport())
	{
		TacticalMap->RemoveFromParent();
		return;
	}
	if (IsMenuOpen()) return;
	if (auto* I = Interaction(); I && I->InventoryOpen) RequestOpenInventory(nullptr, false);
	if (!TacticalMap) TacticalMap = CreateWidget<UExtractionTacticalMap>(this);
	if (TacticalMap)
	{
		TacticalMap->SetVisibility(ESlateVisibility::HitTestInvisible);
		TacticalMap->AddToViewport(80);
	}
}

// 通过当前 Pawn 获取交互组件，观战没有 Pawn 时返回空。
UExtractionInteractionComponent* AExtractionPlayerController::Interaction() const
{
	const auto* C = Cast<ABrokenEdgeCharacter>(GetPawn());
	return C ? C->GetExtractionInteraction() : nullptr;
}

// 以本地和复制会话号的较大值递增发请求，打开前先停持续射击，避免延迟期间继续预测。
void AExtractionPlayerController::RequestOpenInventory(AExtractionLootActor* Target, bool bOpen)
{
	if (bOpen && TacticalMap && TacticalMap->IsInViewport()) TacticalMap->RemoveFromParent();
	if (bOpen) StopHeldActions();
	auto* I = Interaction();
	if (!I) return;
	LocalInventorySession = FMath::Max(LocalInventorySession, I->Session) + 1;
	if (bOpen) I->StopCombatInput();
	ServerOpenInventory(LocalInventorySession, Target, bOpen);
}

// 服务器交由交互组件验证会话与目标，随后把操作反馈发回所属客户端。
void AExtractionPlayerController::ServerOpenInventory_Implementation(uint32 Session, AExtractionLootActor* Target,
                                                                     bool bOpen)
{
	if (auto* I = Interaction())
	{
		I->Message.Reset();
		I->Open(Session, Target, bOpen);
		ClientItemResult(0, I->Message);
	}
}

// 仅可行动且未开菜单时切换本人背包，不绕过服务器 Open 流程。
void AExtractionPlayerController::ToggleInventory()
{
	if (TacticalMap && TacticalMap->IsInViewport()) TacticalMap->RemoveFromParent();
	if (auto* I = Interaction(); I && I->CanAct() && !bMenuOpen) RequestOpenInventory(nullptr, !I->InventoryOpen);
}

// 释放交互键时取消救援请求，不直接修改目标生命或进度。
void AExtractionPlayerController::ReleaseInteract()
{
	if (auto* C = Cast<ABrokenEdgeCharacter>(GetPawn())) C->GetExtractionAbilities()->ReleaseRescue();
}

// 选择本地治疗焦点后发请求，服务器仍检查友方、距离和血量。
void AExtractionPlayerController::HealAbility()
{
	// 按下只记时刻；松开时判断是轻点（切枪）还是长按（Tick 里到时就给自己打针）。
	if (auto* C = Cast<ABrokenEdgeCharacter>(GetPawn()); C && !bMenuOpen)
	{
		HealDownAt = GetWorld()->GetTimeSeconds();
		bHealHoldUsed = false;
	}
}

void AExtractionPlayerController::HealReleased()
{
	auto* C = Cast<ABrokenEdgeCharacter>(GetPawn());
	if (C && HealDownAt >= 0 && !bHealHoldUsed && !bMenuOpen)
	{
		auto* Abilities = C->GetExtractionAbilities();
		Abilities->SetStimDrawn(!Abilities->bStimDrawn);
	}
	HealDownAt = -1;
	bHealHoldUsed = false;
}

void AExtractionPlayerController::StopHeldActions()
{
	bAimingGrenade = false;
	bAimingGrenadeLow = false;
	HealDownAt = -1;
	bHealHoldUsed = false;
	if (auto* C = Cast<ABrokenEdgeCharacter>(GetPawn()))
	{
		C->StopHeldCombatInput();
		C->GetExtractionAbilities()->ReleaseRescue();
	}
}

void AExtractionPlayerController::HandleApplicationActivation(bool bActive)
{
	if (!bActive && IsLocalController()) StopHeldActions();
}

void AExtractionPlayerController::OnPossess(APawn* InPawn)
{
	StopHeldActions();
	Super::OnPossess(InPawn);
	StopHeldActions();
}

void AExtractionPlayerController::OnUnPossess()
{
	StopHeldActions();
	Super::OnUnPossess();
}

// 发起烟雾能力请求（高抛），出手方向与冷却在服务器处理。
void AExtractionPlayerController::SmokeAbility()
{
	if (auto* C = Cast<ABrokenEdgeCharacter>(GetPawn()); C && !bMenuOpen)
		C->GetExtractionAbilities()->Request(
			EExtractionAction::Smoke);
}

// Alt+X 低抛：初速减半、出手点更低，用来把烟丢到脚前或门口。
void AExtractionPlayerController::SmokeAbilityLow()
{
	if (auto* C = Cast<ABrokenEdgeCharacter>(GetPawn()); C && !bMenuOpen)
		C->GetExtractionAbilities()->Request(EExtractionAction::Smoke, nullptr, true);
}

// 交互优先救援，其次钥匙门、战利品；没有目标时才尝试切枪，避免同一按键触发多种动作。
void AExtractionPlayerController::Interact()
{
	if (auto* C = Cast<ABrokenEdgeCharacter>(GetPawn()); C && !bMenuOpen)
		if (auto* Other = C->GetExtractionAbilities()->RescueFocus())
		{
			C->GetExtractionAbilities()->Request(EExtractionAction::Revive, Other);
			return;
		}
	auto* I = Interaction();
	auto* C = Cast<ABrokenEdgeCharacter>(GetPawn());
	if (!I || !I->CanAct() || I->InventoryOpen || bMenuOpen) return;
	if (I->ZiplineFocus) ServerUseZipline(I->ZiplineFocus);
	else if (I->SwitchFocus) ServerPullSwitch(I->SwitchFocus);
	else if (I->DoorFocus) ServerUnlockDoor(I->DoorFocus, I->Inventory()->GetContents().Revision);
	else if (I->Focus) RequestOpenInventory(I->Focus, true);
	else if (C) ServerSwapExtractionWeapons();
}

void AExtractionPlayerController::InteractDoor()
{
	// F 键专用入口：只开门，不做换枪/救人，避免和现有 Interact 语义互相干扰。
	auto* I = Interaction();
	if (!I || !I->CanAct() || I->InventoryOpen || bMenuOpen) return;
	if (I->ZiplineFocus)
	{
		ServerUseZipline(I->ZiplineFocus);
		return;
	}
	if (I->SwitchFocus)
	{
		ServerPullSwitch(I->SwitchFocus);
		return;
	}
	if (I->OrdinaryDoorFocus) { ServerToggleDoor(I->OrdinaryDoorFocus); return; }
	auto* Inventory = I->Inventory();
	if (!Inventory || !I->DoorFocus) return;
	if (!I->DoorFocus->IsInInteractionRange(GetPawn())) return;
	ServerUnlockDoor(I->DoorFocus, Inventory->GetContents().Revision);
}

// 先确认门对象有效且属于同一世界，再由门执行完整权限和库存版本校验。
void AExtractionPlayerController::ServerUnlockDoor_Implementation(AExtractionKeyRoom* Door, int32 Revision)
{
	if (IsValid(Door) && Door->GetWorld() == GetWorld())
		ClientItemResult(0, Door->Unlock(Interaction(), Revision));
}

void AExtractionPlayerController::ThrowGrenade()
{
	if (auto* I = Interaction(); I && I->CanAct() && !I->InventoryOpen && !bMenuOpen)
	{
		bAimingGrenade = true;
		// Alt+G 会同时触发 G 和 Alt+G 两条绑定，直接读 Alt 键状态，与触发顺序无关。
		bAimingGrenadeLow = IsInputKeyDown(EKeys::LeftAlt) || IsInputKeyDown(EKeys::RightAlt);
	}
}

void AExtractionPlayerController::PlacePing()
{
	auto* I = Interaction();
	if (!I || !I->CanAct() || I->InventoryOpen || bMenuOpen || !GetPawn()) return;
	FVector Eye;
	FRotator View;
	GetPlayerViewPoint(Eye, View);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ExtractionPing), false, GetPawn());
	const FVector End = Eye + View.Vector() * 30000.f;
	ServerPing(GetWorld()->LineTraceSingleByChannel(Hit, Eye, End, ECC_Visibility, Params) ? Hit.ImpactPoint : End);
}

void AExtractionPlayerController::ServerPing_Implementation(FVector_NetQuantize Location)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const auto* PS = GetPlayerState<AExtractionPlayerState>();
	// 限频并只允许存活或倒地的玩家标点，避免刷屏。
	if (Now < NextPingAt || !PS || (PS->Life != EExtractionLife::Alive && PS->Life != EExtractionLife::Downed)) return;
	NextPingAt = Now + .5;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		if (auto* Mate = Cast<AExtractionPlayerController>(It->Get()))
			if (const auto* MatePS = Mate->GetPlayerState<AExtractionPlayerState>(); MatePS && MatePS->ExtractionSquad == PS->
				ExtractionSquad)
				Mate->ClientPing(Location, PS->GetPlayerName(), Mate == this);
}

void AExtractionPlayerController::ClientPing_Implementation(FVector_NetQuantize Location, const FString& Sender, bool bSelf)
{
	Pings.RemoveAll([&](const FPing& P) { return P.Sender == Sender; });
	Pings.Add({GetWorld()->GetTimeSeconds(), Location, Sender, bSelf});
}

void AExtractionPlayerController::ThrowGrenadeLow()
{
	if (auto* I = Interaction(); I && I->CanAct() && !I->InventoryOpen && !bMenuOpen)
	{
		bAimingGrenade = true;
		bAimingGrenadeLow = true;
	}
}

// 服务器校验行动能力与投掷间隔，从背包（不含安全箱）取出一枚投掷物再生成手雷。
void AExtractionPlayerController::ServerThrowGrenade_Implementation(bool bLow)
{
	auto* C = Cast<ABrokenEdgeCharacter>(GetPawn());
	auto* I = Interaction();
	auto* Inv = I ? I->Inventory() : nullptr;
	const double Now = GetWorld()->GetTimeSeconds();
	const auto* R = GetDefault<UExtractionRules>();
	if (!C || !I || !Inv || !I->CanAct() || Now < NextGrenadeAt
		|| C->GetCombatState() != ECombatState::ECS_Unoccuiped) return;
	const auto* Item = Inv->GetContents().Items.FindByPredicate([](const FExtractionItem& Entry)
	{
		const auto* D = UExtractionItemDefinition::Get(Entry.Definition);
		return D && D->Kind == EExtractionItemKind::Throwable && Entry.Slot != ExtractionGear::SafeGrid;
	});
	if (!Item)
	{
		ClientItemResult(0, TEXT("没有可投掷的手雷"));
		return;
	}
	FVector Eye;
	FRotator View;
	C->GetActorEyesViewPoint(Eye, View);
	View.Pitch = FMath::Clamp(View.Pitch + R->FragThrowPitch, -89.f, 89.f);
	const FVector From = Eye + View.Vector() * 40.f;
	FActorSpawnParameters Params;
	Params.Owner = C;
	Params.Instigator = C;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Grenade = GetWorld()->SpawnActor<AExtractionFragGrenade>(From, View, Params);
	if (!Grenade) return;
	const FGuid Used = Item->Id;
	const auto Consumed = Inv->ConsumeOne(Used);
	if (!Consumed.Ok())
	{
		Grenade->Destroy();
		ClientItemResult(0, Consumed.Error);
		return;
	}
	NextGrenadeAt = Now + R->FragThrowInterval;
	Grenade->Launch(View.Vector() * (bLow ? R->FragThrowLowSpeed : R->FragThrowSpeed) + C->GetVelocity());
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_FRAG_THROW Player=%s Low=%d"), *GetNameSafe(PlayerState), bLow ? 1 : 0);
}

void AExtractionPlayerController::ServerUseZipline_Implementation(AExtractionZipline* Line)
{
	if (!IsValid(Line) || Line->GetWorld() != GetWorld()) return;
	const FString Error = Line->Use(Cast<ABrokenEdgeCharacter>(GetPawn()));
	if (!Error.IsEmpty()) ClientItemResult(0, Error);
}

void AExtractionPlayerController::ServerPullSwitch_Implementation(AExtractionExitSwitch* Switch)
{
	if (IsValid(Switch) && Switch->GetWorld() == GetWorld())
		ClientItemResult(0, Switch->Pull(Cast<ABrokenEdgeCharacter>(GetPawn())));
}

// 统一填入递增请求序号、当前交互会话和两侧容器版本，再发送物品 RPC。
void AExtractionPlayerController::RequestItem(FExtractionItemRequest Request)
{
	auto* I = Interaction();
	if (!I || !I->Inventory()) return;
	Request.Sequence = ++LocalItemSequence;
	if (Request.Op == EExtractionItemOp::Use) I->StopCombatInput();
	Request.Session = I->Session;
	Request.InventoryRevision = I->Inventory()->GetContents().Revision;
	Request.LootRevision = I->LootView.Revision;
	ServerItemOperation(Request);
}

void AExtractionPlayerController::ClientCombatHit_Implementation(uint8 Result, const FString& Victim)
{
	const double Now = GetWorld()->GetTimeSeconds();
	// 同一帧里击杀优先于普通命中，标记不被后到的小结果覆盖。
	if (Now - LastHitAt > .05 || Result >= LastHitResult) LastHitResult = Result;
	LastHitAt = Now;
	if (Result > 0)
	{
		CombatNotices.Add({Now, (Result == 2 ? TEXT("击杀  ") : TEXT("击倒  ")) + Victim});
		if (CombatNotices.Num() > 4) CombatNotices.RemoveAt(0);
	}
}

void AExtractionPlayerController::RecordDamage(const FString& Killer, const FString& Weapon, float Distance, float Damage)
{
	DeathRecap.Killer = Killer;
	DeathRecap.Weapon = Weapon;
	DeathRecap.Distance = Distance;
	++DeathRecap.Hits;
	DeathRecap.Damage += Damage;
}

void AExtractionPlayerController::SendDeathRecap(bool bBledOut)
{
	DeathRecap.bBledOut = bBledOut;
	ClientDeathRecap(DeathRecap);
	DeathRecap = FExtractionDeathRecap();
}

void AExtractionPlayerController::ClientDeathRecap_Implementation(const FExtractionDeathRecap& Recap)
{
	DeathRecap = Recap;
	DeathRecapAt = GetWorld()->GetTimeSeconds();
}

void AExtractionPlayerController::ClientSquadNotice_Implementation(const FString& Text)
{
	CombatNotices.Add({GetWorld()->GetTimeSeconds(), Text});
	if (CombatNotices.Num() > 4) CombatNotices.RemoveAt(0);
}

void AExtractionPlayerController::ClientDamaged_Implementation(FVector_NetQuantize From)
{
	DamageSources.Add({GetWorld()->GetTimeSeconds(), From});
	if (DamageSources.Num() > 6) DamageSources.RemoveAt(0);
}

// 服务器将请求交给 Execute 事务入口，回执包含该序号与结果文本。
void AExtractionPlayerController::ServerItemOperation_Implementation(FExtractionItemRequest Request)
{
	if (auto* I = Interaction()) ClientItemResult(Request.Sequence, I->Execute(Request));
}

void AExtractionPlayerController::ShowItemMessage(const FString& Message, float Seconds)
{
	ItemMessage = Message;
	TimedItemMessage = Message;
	// 空文本表示"清空提示"，不需要计时。
	ItemMessageUntil = Message.IsEmpty() ? 0.0 : GetWorld()->GetTimeSeconds() + FMath::Max(0.f, Seconds);
}

// 拥有者记录结果消息和已回执水位；库存本身仍等待属性复制更新。
void AExtractionPlayerController::ClientItemResult_Implementation(uint32 Sequence, const FString& Error)
{
	ShowItemMessage(Error);
	ItemResultSequence = FMath::Max(ItemResultSequence, Sequence);
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_ITEM_RESULT Sequence=%u Error=%s"), Sequence, *Error);
}

// 取消服务器医疗计时，不在客户端提前消耗或恢复物品。
void AExtractionPlayerController::ServerCancelMedical_Implementation()
{
	if (auto* I = Interaction()) I->CancelMedical();
}

// 服务器检查行动与背包状态，取消医疗后再调用既有切枪入口。
void AExtractionPlayerController::ServerSwapExtractionWeapons_Implementation()
{
	if (auto* I = Interaction(); I && I->CanAct() && !I->InventoryOpen)
	{
		I->CancelMedical();
		auto* C = Cast<ABrokenEdgeCharacter>(GetPawn());
		if (C->GetCombat()->ShouldSwapWeapons()) C->GetCombat()->SwapWeapons();
	}
}

// 快捷医疗只选择本人背包的 Medkit；正在治疗时同一输入改为取消。
void AExtractionPlayerController::UseMedical()
{
	auto* I = Interaction();
	if (!I || !I->CanAct()) return;
	if (I->MedicalEnd > 0)
	{
		ServerCancelMedical();
		return;
	}
	for (const auto& Item : I->Inventory()->GetContents().Items)
		if (Item.Definition == TEXT("Medkit"))
		{
			FExtractionItemRequest R;
			R.Item = Item.Id;
			R.Op = EExtractionItemOp::Use;
			RequestItem(R);
			return;
		}
	ShowItemMessage(TEXT("没有医疗包"));
}

// 把手持槽物品转换为 Drop 请求，复用背包事务而非直接销毁武器 Actor。
void AExtractionPlayerController::DropEquippedItem()
{
	auto* I = Interaction();
	if (!I || !I->CanAct()) return;
	if (const auto* Item = I->Inventory()->HeldGun())
	{
		FExtractionItemRequest R;
		R.Item = Item->Id;
		R.Op = EExtractionItemOp::Drop;
		RequestItem(R);
	}
}

// 服务器销毁控制器时触发退出掉落清理，防重复由背包组件维护。
void AExtractionPlayerController::Destroyed()
{
	if (HasAuthority()) if (auto* I = Interaction()) I->DropOnExit();
	Super::Destroyed();
}

// 服务器保存当前视点、解除 Pawn 控制并切换观战状态，再选择合法队友目标。
void AExtractionPlayerController::BeginExtractionSpectating()
{
	if (!HasAuthority()) return;
	GetPlayerViewPoint(LastSpectatorLocation, LastSpectatorRotation);
	UnPossess();
	bExtractionSpectating = true;
	ChangeState(NAME_Spectating);
	ClientGotoState(NAME_Spectating);
	SelectSpectatorTarget(1);
	ForceNetUpdate();
}

// 仅在在线、存活、有 Pawn 的同小队成员中按玩家 ID 排序循环选择，不能观战敌人。
void AExtractionPlayerController::SelectSpectatorTarget(int32 Direction)
{
	const auto* GS = GetWorld()->GetGameState<AExtractionGameState>();
	if (!GS) return;
	const auto* OwnPS = GetPlayerState<AExtractionPlayerState>();
	TArray<AExtractionPlayerState*> Targets;
	for (APlayerState* State : GS->PlayerArray)
		if (auto* PS = Cast<AExtractionPlayerState>(State); PS && PS->Life == EExtractionLife::Alive && PS->GetPawn() &&
			!PS->IsInactive() && OwnPS && OwnPS->ExtractionSquad > 0 && PS->ExtractionSquad == OwnPS->ExtractionSquad)
			Targets.Add(PS);
	Targets.Sort([](const AExtractionPlayerState& A, const AExtractionPlayerState& B)
	{
		return A.GetPlayerId() < B.GetPlayerId();
	});
	const int32 Current = Targets.IndexOfByKey(SpectatedPlayer.Get());
	SpectatedPlayer = Targets.IsEmpty()
		                  ? nullptr
		                  : Targets[(Current + (Direction < 0 ? -1 : 1) + Targets.Num()) % Targets.Num()];
	ForceNetUpdate();
}

// 服务器只接受观战者的 +1/-1 切换请求，拒绝任意方向和仍存活玩家。
void AExtractionPlayerController::ServerCycleSpectator_Implementation(int32 Direction)
{
	const auto* PS = GetPlayerState<AExtractionPlayerState>();
	if (bExtractionSpectating && PS && PS->Life != EExtractionLife::Alive && (Direction == 1 || Direction == -1))
		SelectSpectatorTarget(Direction);
}

// 远端观战连接在服务器的相关性视点跟随所选队友，避免仍用撤离点附近的旧相机位置。
void AExtractionPlayerController::GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	// The local orbit camera is separate from the spectator pawn. Its synced
	// location can remain at extraction, so server relevancy must follow the
	// living teammate selected by the authority instead.
	if (HasAuthority() && !IsLocalController() && bExtractionSpectating && SpectatedPlayer
		&& SpectatedPlayer->Life == EExtractionLife::Alive)
	{
		if (const APawn* Target = SpectatedPlayer->GetPawn())
		{
			Target->GetActorEyesViewPoint(OutLocation, OutRotation);
			return;
		}
	}
	Super::GetPlayerViewPoint(OutLocation, OutRotation);
}

// 服务器修复失效观战目标；本地创建并刷新界面、切输入模式，再维护避障观战相机。
// 目标区域尚未可见时保留旧视点，避免相机提前进入未加载场景。
void AExtractionPlayerController::Tick(float Delta)
{
	Super::Tick(Delta);
	if (IsLocalController())
	{
		const auto* I = Interaction();
		const bool bActionable = I && I->CanAct() && !I->InventoryOpen && !bMenuOpen;
		if (bInputWasActionable && !bActionable) StopHeldActions();
		bInputWasActionable = bActionable;
	}
	// 长按 V 到时：给自己打针（播自用动作），松开时不再切枪。
	if (HealDownAt >= 0 && !bHealHoldUsed && IsLocalController()
		&& GetWorld()->GetTimeSeconds() - HealDownAt >= HealHoldSeconds)
	{
		bHealHoldUsed = true;
		if (auto* C = Cast<ABrokenEdgeCharacter>(GetPawn()); C && !bMenuOpen)
			C->GetExtractionAbilities()->Request(EExtractionAction::Heal, C);
	}
	// 手雷瞄准：打开背包/菜单或失去行动能力即取消；松开 G 时投出。
	if (bAimingGrenade && IsLocalController())
	{
		const auto* I = Interaction();
		if (!I || !I->CanAct() || I->InventoryOpen || bMenuOpen) bAimingGrenade = false;
		else if (!IsInputKeyDown(EKeys::G))
		{
			bAimingGrenade = false;
			ServerThrowGrenade(bAimingGrenadeLow);
		}
	}
	// 一次性提示到点自动消失；界面里直接给 ItemMessage 赋值的也按文本变化重新计时。
	const double Now = GetWorld()->GetTimeSeconds();
	if (ItemMessage.IsEmpty())
	{
		ItemMessageUntil = 0.0;
		TimedItemMessage.Empty();
	}
	else
	{
		if (ItemMessage != TimedItemMessage)
		{
			TimedItemMessage = ItemMessage;
			ItemMessageUntil = Now + ItemMessageSeconds;
		}
		if (Now >= ItemMessageUntil)
		{
			ItemMessage.Empty();
			TimedItemMessage.Empty();
			ItemMessageUntil = 0.0;
		}
	}
	auto* GS = GetWorld()->GetGameState<AExtractionGameState>();
	if (!GS) return;
	if (HasAuthority() && bExtractionSpectating && (!SpectatedPlayer || SpectatedPlayer->Life != EExtractionLife::Alive
		|| !SpectatedPlayer->GetPawn()))
		SelectSpectatorTarget(1);
	if (!IsLocalController() || !GetLocalPlayer()) return;
	if (auto* ControlledCharacter = Cast<ABrokenEdgeCharacter>(GetPawn()))
	{
		// The owning client also stops prediction while waiting for all players to load.
		if (GS->Phase != EExtractionPhase::Active || !GS->StorageError.IsEmpty() || ControlledCharacter->IsDefeated())
			ControlledCharacter->GetCharacterMovement()->DisableMovement();
		else if (ControlledCharacter->GetCharacterMovement()->MovementMode == MOVE_None)
			ControlledCharacter->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
	// 光标与 GameAndUI 模式跟随菜单、观战和背包状态切换，不能只隐藏控件而保留错误输入模式。
	const bool ShowMenu = bMenuOpen || bExtractionSpectating || GS->Phase == EExtractionPhase::Ended
		|| (Interaction() && Interaction()->InventoryOpen);
	if (ShowMenu != bShowMouseCursor)
	{
		SetShowMouseCursor(ShowMenu);
		if (ShowMenu) SetInputMode(FInputModeGameAndUI());
		else SetInputMode(FInputModeGameOnly());
	}
	if (ExtractionWidget) ExtractionWidget->Refresh();
	if (InventoryWidget) InventoryWidget->Refresh();
	if (!bExtractionSpectating)
	{
		GetPlayerViewPoint(LastSpectatorLocation, LastSpectatorRotation);
		return;
	}
	if (!SpectatorCamera)
	{
		SpectatorCamera = GetWorld()->SpawnActor<ACameraActor>(LastSpectatorLocation, LastSpectatorRotation);
		SetViewTarget(SpectatorCamera);
	}
	if (const APawn* Target = SpectatedPlayer ? SpectatedPlayer->GetPawn() : nullptr)
	{
		if (const auto* Region = AExtractionRegionStream::Find(GetWorld()); Region && !Region->IsViewReady(
			Target->GetActorLocation()))
			return;
		const FVector Focus = Target->GetActorLocation() + FVector(0, 0, 65);
		const FVector Desired = Focus - Target->GetActorForwardVector() * 350 + FVector(0, 0, 110);
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(ExtractionSpectator), false, Target);
		const bool Blocked = GetWorld()->SweepSingleByChannel(Hit, Focus, Desired, FQuat::Identity, ECC_Camera,
		                                                      FCollisionShape::MakeSphere(12), Query);
		LastSpectatorLocation = Blocked ? Hit.Location : Desired;
		LastSpectatorRotation = (Focus - LastSpectatorLocation).Rotation();
	}
	SpectatorCamera->SetActorLocationAndRotation(LastSpectatorLocation, LastSpectatorRotation);
}

// 服务器用 bLeaving 防止重复离开，调用 GameMode 离局处理后通知本机清理会话。
void AExtractionPlayerController::ServerLeaveExtraction_Implementation()
{
	if (bLeaving) return;
	if (auto* Mode = GetWorld()->GetAuthGameMode<AExtractionGameMode>())
	{
		bLeaving = true;
		Mode->LeavePlayer(this);
		ClientLeaveExtraction();
	}
}

// 所属客户端开始销毁本机会话，再返回大厅地图。
void AExtractionPlayerController::ClientLeaveExtraction_Implementation()
{
	DestroyLocalSession();
}

// 无会话直接返回，有异步操作时延迟重试，否则绑定销毁回调后发请求。
void AExtractionPlayerController::DestroyLocalSession()
{
	auto* Sessions = GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>();
	if (!Sessions || !Sessions->HasSession())
	{
		LocalSessionDestroyed(true);
		return;
	}
	if (Sessions->IsBusy())
	{
		GetWorldTimerManager().SetTimer(LeaveRetryTimer, this, &ThisClass::DestroyLocalSession, .25f);
		return;
	}
	Sessions->MultiplayerOnDestorySessionComplete.AddUniqueDynamic(this, &ThisClass::LocalSessionDestroyed);
	Sessions->DestorySession();
}

// 解除回调绑定；失败延迟重试，成功后打开大厅，避免遗留旧会话。
void AExtractionPlayerController::LocalSessionDestroyed(bool bSuccess)
{
	if (auto* Sessions = GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>())
		Sessions->MultiplayerOnDestorySessionComplete.RemoveDynamic(this, &ThisClass::LocalSessionDestroyed);
	if (!bSuccess)
	{
		GetWorldTimerManager().SetTimer(LeaveRetryTimer, this, &ThisClass::DestroyLocalSession, .5f);
		return;
	}
	UGameplayStatics::OpenLevel(this, TEXT("/Game/Maps/Lobby"), true);
}

// 移除本地 UI、观战相机、重试定时器和会话委托，防止关卡切换后仍收到旧回调。
void AExtractionPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	StopHeldActions();
	if (FSlateApplication::IsInitialized())
		FSlateApplication::Get().OnApplicationActivationStateChanged().RemoveAll(this);
	if (TacticalMap) TacticalMap->RemoveFromParent();
	GetWorldTimerManager().ClearTimer(LeaveRetryTimer);
	if (ExtractionWidget) ExtractionWidget->RemoveFromParent();
	if (InventoryWidget) InventoryWidget->RemoveFromParent();
	ExtractionWidget = nullptr;
	InventoryWidget = nullptr;
	bLocalWidgetsAttempted = false;
	if (SpectatorCamera) SpectatorCamera->Destroy();
	if (auto* Sessions = GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>())
		Sessions->MultiplayerOnDestorySessionComplete.RemoveDynamic(this, &ThisClass::LocalSessionDestroyed);
	Super::EndPlay(Reason);
}


void AExtractionPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController() && FSlateApplication::IsInitialized())
		FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(this, &ThisClass::HandleApplicationActivation);
	EnsureLocalWidgets();
}

void AExtractionPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();
	EnsureLocalWidgets();
}

void AExtractionPlayerController::PostSeamlessTravel()
{
	Super::PostSeamlessTravel();
	bLocalWidgetsAttempted = false;
	EnsureLocalWidgets();
}

void AExtractionPlayerController::EnsureLocalWidgets()
{
	UE_LOG(LogTemp, Verbose, TEXT("EXTRACTION_UI_INIT pc=%s begun=%d local=%d player=%d attempted=%d"), *GetName(),
	       HasActorBegunPlay(), IsLocalController(), GetLocalPlayer() != nullptr, bLocalWidgetsAttempted);
	if (!HasActorBegunPlay() || !IsLocalController() || !GetLocalPlayer() || bLocalWidgetsAttempted) return;
	bLocalWidgetsAttempted = true;
	if (!ExtractionWidget)
	{
		if (UClass* Class = ExtractionWidgetClass.LoadSynchronous())
			ExtractionWidget = CreateWidget<UExtractionWidget>(this, Class);
		if (!ExtractionWidget) UE_LOG(LogTemp, Error, TEXT("EXTRACTION_HUD_CREATE_FAILED %s"),
		                              *ExtractionWidgetClass.ToString());
	}
	if (ExtractionWidget && !ExtractionWidget->IsInViewport()) ExtractionWidget->AddToViewport(50);
	if (!InventoryWidget)
	{
		if (UClass* Class = InventoryWidgetClass.LoadSynchronous())
			InventoryWidget = CreateWidget<ULootingMainView>(this, Class);
		if (!InventoryWidget) UE_LOG(LogTemp, Error, TEXT("EXTRACTION_INVENTORY_CREATE_FAILED %s"),
		                             *InventoryWidgetClass.ToString());
	}
	if (InventoryWidget && !InventoryWidget->IsInViewport()) InventoryWidget->AddToViewport(60);
}

void AExtractionPlayerController::ServerToggleDoor_Implementation(AExtractionDoor* Door)
{
	const auto* I = Interaction();
	if (IsValid(Door) && Door->GetWorld() == GetWorld() && I && !I->InventoryOpen)
		ClientItemResult(0, Door->Toggle(Cast<ABrokenEdgeCharacter>(GetPawn())));
}
