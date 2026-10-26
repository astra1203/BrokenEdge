#include "ExtractionPlayerController.h"
#include "ExtractionTacticalMap.h"
#include "BrokenEdge/Traversal/ExtractionTraversalComponent.h"
#include "ExtractionRegionStream.h"
#include "ExtractionHealth.h"
#include "ExtractionMeleeComponent.h"
#include "ExtractionInteractionComponent.h"
#include "ExtractionKeyRoom.h"
#include "ExtractionAbilities.h"
#include "ExtractionSmoke.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionLootActor.h"
#include "ExtractionInventoryWidget.h"
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
#include "SessionWidgetStyle.h"
#include "TimerManager.h"
#include "Components/ProgressBar.h"
// 撤离控制器把本地输入转为服务器 RPC，并管理 HUD、背包、观战和离开会话。
// 本地界面只读复制状态；物品结果、撤离成功和观战资格由服务器决定。此文件同时实现 UExtractionWidget。

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
	InputComponent->BindKey(EKeys::Z, IE_Pressed, this, &ThisClass::HealAbility);
	InputComponent->BindKey(EKeys::X, IE_Pressed, this, &ThisClass::SmokeAbility);
	InputComponent->BindKey(EKeys::E, IE_Released, this, &ThisClass::ReleaseInteract);
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ThisClass::SelectKnife);
	InputComponent->BindKey(EKeys::One, IE_Pressed, this, &ThisClass::SelectHeldGun);
	InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ThisClass::SelectOtherGun);
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
	if (auto* C = Cast<ABrokenEdgeCharacter>(GetPawn()); C && !bMenuOpen)
		C->GetExtractionAbilities()->Request(EExtractionAction::Heal, C->GetExtractionAbilities()->HealFocus());
}

// 发起烟雾能力请求，落点与冷却在服务器处理。
void AExtractionPlayerController::SmokeAbility()
{
	if (auto* C = Cast<ABrokenEdgeCharacter>(GetPawn()); C && !bMenuOpen)
		C->GetExtractionAbilities()->Request(
			EExtractionAction::Smoke);
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
	if (I->DoorFocus) ServerUnlockDoor(I->DoorFocus, I->Inventory()->Contents.Revision);
	else if (I->Focus) RequestOpenInventory(I->Focus, true);
	else if (C) ServerSwapExtractionWeapons();
}

void AExtractionPlayerController::InteractDoor()
{
	// F 键专用入口：只开门，不做换枪/救人，避免和现有 Interact 语义互相干扰。
	auto* I = Interaction();
	if (!I || !I->CanAct() || I->InventoryOpen || bMenuOpen) return;
	auto* Inventory = I->Inventory();
	if (!Inventory || !I->DoorFocus) return;
	if (!I->DoorFocus->IsInInteractionRange(GetPawn())) return;
	ServerUnlockDoor(I->DoorFocus, Inventory->Contents.Revision);
}

// 先确认门对象有效且属于同一世界，再由门执行完整权限和库存版本校验。
void AExtractionPlayerController::ServerUnlockDoor_Implementation(AExtractionKeyRoom* Door, int32 Revision)
{
	if (IsValid(Door) && Door->GetWorld() == GetWorld())
		ClientItemResult(0, Door->Unlock(Interaction(), Revision));
}

// 统一填入递增请求序号、当前交互会话和两侧容器版本，再发送物品 RPC。
void AExtractionPlayerController::RequestItem(FExtractionItemRequest Request)
{
	auto* I = Interaction();
	if (!I || !I->Inventory()) return;
	Request.Sequence = ++LocalItemSequence;
	if (Request.Op == EExtractionItemOp::Use) I->StopCombatInput();
	Request.Session = I->Session;
	Request.InventoryRevision = I->Inventory()->Contents.Revision;
	Request.LootRevision = I->LootView.Revision;
	ServerItemOperation(Request);
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
	for (const auto& Item : I->Inventory()->Contents.Items)
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
	if (const auto* Item = I->Inventory()->Contents.InSlot(0))
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
	if (!ExtractionWidget)
	{
		ExtractionWidget = CreateWidget<UExtractionWidget>(this);
		ExtractionWidget->AddToViewport(50);
	}
	if (!InventoryWidget)
	{
		InventoryWidget = CreateWidget<UExtractionInventoryWidget>(this);
		InventoryWidget->AddToViewport(60);
	}
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
	ExtractionWidget->Refresh();
	InventoryWidget->Refresh();
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
	if (TacticalMap) TacticalMap->RemoveFromParent();
	GetWorldTimerManager().ClearTimer(LeaveRetryTimer);
	if (ExtractionWidget) ExtractionWidget->RemoveFromParent();
	if (InventoryWidget) InventoryWidget->RemoveFromParent();
	if (SpectatorCamera) SpectatorCamera->Destroy();
	if (auto* Sessions = GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>())
		Sessions->MultiplayerOnDestorySessionComplete.RemoveDynamic(this, &ThisClass::LocalSessionDestroyed);
	Super::EndPlay(Reason);
}

// 构建 HUD 控件树并绑定按钮，展示层只保存控件引用，不创建权威对局状态。
bool UExtractionWidget::Initialize()
{
	if (!Super::Initialize()) return false;
	auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Canvas;
	SmokeHaze = WidgetTree->ConstructWidget<UBorder>();
	SmokeHaze->SetBrushColor(FLinearColor(.34f, .34f, .34f, 1.f));
	SmokeHaze->SetVisibility(ESlateVisibility::Collapsed);
	auto* HazeSlot = Canvas->AddChildToCanvas(SmokeHaze);
	HazeSlot->SetAnchors(FAnchors(0, 0, 1, 1));
	HazeSlot->SetOffsets(FMargin(0));
	auto* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrushColor(FLinearColor(0.015f, 0.025f, 0.04f, .85f));
	Panel->SetPadding(FMargin(14));
	auto* PanelSlot = Canvas->AddChildToCanvas(Panel);
	PanelSlot->SetPosition(FVector2D(18, 18));
	PanelSlot->SetAutoSize(true);
	auto* Box = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->AddChild(Box);
	Status = SessionWidgetStyle::Text(WidgetTree, TEXT("正在加载航天基地…"), 16);
	Status->SetAutoWrapText(true);
	Status->SetWrapTextAt(400);
	Status->SetMinDesiredWidth(400);
	SessionWidgetStyle::Line(Box, Status, 8);
	Previous = SessionWidgetStyle::Button(WidgetTree, TEXT("PreviousSpectator"), TEXT("上一位"));
	Next = SessionWidgetStyle::Button(WidgetTree, TEXT("NextSpectator"), TEXT("下一位"));
	Leave = SessionWidgetStyle::Button(WidgetTree, TEXT("LeaveExtraction"), TEXT("离开对局 · 返回房间列表"));
	Previous->OnClicked.AddUniqueDynamic(this, &ThisClass::PreviousClicked);
	Next->OnClicked.AddUniqueDynamic(this, &ThisClass::NextClicked);
	Leave->OnClicked.AddUniqueDynamic(this, &ThisClass::LeaveClicked);
	SessionWidgetStyle::Line(Box, Previous, 5);
	SessionWidgetStyle::Line(Box, Next, 5);
	SessionWidgetStyle::Line(Box, Leave, 0);
	Vitals = SessionWidgetStyle::Text(WidgetTree, TEXT(""), 20);
	auto* VitalsSlot = Canvas->AddChildToCanvas(Vitals);
	VitalsSlot->SetAnchors(FAnchors(0, 1));
	VitalsSlot->SetAlignment(FVector2D(0, 1));
	VitalsSlot->SetPosition(FVector2D(24, -24));
	VitalsSlot->SetSize(FVector2D(440, 100));
	const auto AddText = [&](TObjectPtr<UTextBlock>& Target, FAnchors Anchor, FVector2D Alignment,
	                         FVector2D Position, FVector2D Size, int32 FontSize)
	{
		Target = SessionWidgetStyle::Text(WidgetTree, TEXT(""), FontSize);
		Target->SetAutoWrapText(true);
		Target->SetVisibility(ESlateVisibility::HitTestInvisible);
		auto* Slot = Canvas->AddChildToCanvas(Target);
		Slot->SetAnchors(Anchor);
		Slot->SetAlignment(Alignment);
		Slot->SetPosition(Position);
		Slot->SetSize(Size);
	};
	AddText(WeaponStatus, FAnchors(1, 1), FVector2D(1, 1), FVector2D(-28, -48), FVector2D(380, 100), 24);
	WeaponStatus->SetJustification(ETextJustify::Right);
	AddText(ExitStatus, FAnchors(1, 0), FVector2D(1, 0), FVector2D(-28, 72), FVector2D(350, 260), 17);
	ExitStatus->SetJustification(ETextJustify::Right);
	ExitStatus->SetColorAndOpacity(FSlateColor(FLinearColor(.42f, .95f, .77f)));
	AddText(Compass, FAnchors(.5f, 0), FVector2D(.5f, 0), FVector2D(0, 22), FVector2D(360, 36), 20);
	Compass->SetJustification(ETextJustify::Center);
	AddText(ExtractionCountdown, FAnchors(.5f, .75f), FVector2D(.5f, 0), FVector2D(0, 0), FVector2D(450, 70), 22);
	ExtractionCountdown->SetJustification(ETextJustify::Center);
	const auto AddBar = [&](TObjectPtr<UProgressBar>& Target, FAnchors Anchor, FVector2D Alignment,
	                        FVector2D Position, FVector2D Size)
	{
		Target = WidgetTree->ConstructWidget<UProgressBar>();
		Target->SetFillColorAndOpacity(FLinearColor(.25f, .9f, .68f));
		Target->SetVisibility(ESlateVisibility::HitTestInvisible);
		auto* Slot = Canvas->AddChildToCanvas(Target);
		Slot->SetAnchors(Anchor);
		Slot->SetAlignment(Alignment);
		Slot->SetPosition(Position);
		Slot->SetSize(Size);
	};
	AddBar(HealthProgress, FAnchors(0, 1), FVector2D(0, 1), FVector2D(24, -132), FVector2D(280, 6));
	AddBar(ExtractionProgress, FAnchors(.5f, .75f), FVector2D(.5f, 0), FVector2D(0, 74), FVector2D(360, 8));
	return true;
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
	SmokeHaze->SetVisibility(AExtractionSmoke::Blocks(GetWorld(), Eye, Eye)
		                         ? ESlateVisibility::HitTestInvisible
		                         : ESlateVisibility::Collapsed);
	const int32 Seconds = FMath::Max(0, FMath::CeilToInt(GS->Deadline - GS->GetServerWorldTimeSeconds()));
	FString Text;
	if (GS->Phase == EExtractionPhase::Waiting) Text = TEXT("等待其他玩家加载…");
	else if (GS->Phase == EExtractionPhase::Ended)
		Text = FString::Printf(
			TEXT("%s\n%d 秒后返回准备大厅"), *GS->EndReason, Seconds);
	else
		Text = FString::Printf(
			TEXT("航天基地 · 小队行动\n剩余 %02d:%02d · 存活 %d\n%s"), Seconds / 60, Seconds % 60, GS->AlivePlayers,
			PS->Life == EExtractionLife::Alive
				? TEXT("Esc 打开对局菜单")
				: PS->Life == EExtractionLife::Downed
				? TEXT("倒地待救援 · Esc 打开菜单")
				: TEXT("本次行动已结束"));
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
	if (PS->Life == EExtractionLife::Alive && GS->Phase == EExtractionPhase::Active && PC->GetPawn())
	{
		for (TActorIterator<AExtractionExit> It(GetWorld()); It; ++It)
		{
			const FString Reason = It->UnavailableReason(GS->GetServerWorldTimeSeconds());
			FString Rule;
			if (It->Definition.bRequireEmptyPack) Rule = TEXT(" · 需清空背包");
			ExitsText += FString::Printf(TEXT("%s\n%.0f 米 · %s%s\n\n"), *It->Definition.Label.ToString(),
			                             FVector::Dist2D(PC->GetPawn()->GetActorLocation(), It->GetActorLocation()) /
			                             100,
			                             Reason.IsEmpty() ? TEXT("可撤离") : *Reason, *Rule);
			// 匹配当前撤离点 ID 后，才用其停留时长计算进度；显示以服务器发布的截止时刻为准。
			if (PS->ExtractAt > 0 && PS->ExtractExitId == It->Definition.Id)
			{
				const double Remaining = FMath::Max(0.0, PS->ExtractAt - GS->GetServerWorldTimeSeconds());
				CountdownText = FString::Printf(TEXT("正在撤离  %.1f\n请留在撤离区域内"), Remaining);
				ExitFraction = FMath::Clamp(1.f - Remaining / It->Definition.HoldSeconds, 0., 1.);
			}
			else if (!PS->ExtractBlockedReason.IsEmpty() && PS->ExtractExitId == It->Definition.Id)
			{
				CountdownText = PS->ExtractBlockedReason;
				ExitFraction = 0;
			}
		}
	}
	ExitStatus->SetText(FText::FromString(ExitsText));
	ExtractionCountdown->SetText(FText::FromString(CountdownText));
	ExtractionProgress->SetPercent(ExitFraction);
	ExtractionProgress->SetVisibility(CountdownText.IsEmpty()
		                                  ? ESlateVisibility::Collapsed
		                                  : ESlateVisibility::HitTestInvisible);
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
			Text += FString::Printf(TEXT("\nZ 治疗 %.0fs · X 烟雾 %.0fs"), FMath::Max(0.0, A->HealReadyAt - Now),
			                        FMath::Max(0.0, A->SmokeReadyAt - Now));
			if (A->RescueFocus()) Text += TEXT("\n按住 E 救援队友 · 5 秒");
		}
		if (A->Action != EExtractionAction::None)
			Text += FString::Printf(
				TEXT("\n%s · %.1f 秒"),
				A->Action == EExtractionAction::Revive
					? TEXT("正在救援")
					: A->Action == EExtractionAction::Heal
					? TEXT("正在治疗")
					: TEXT("投放烟雾"), FMath::Max(0.0, A->ActionEnd - Now));
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
		HealthProgress->SetPercent(
			ControlledCharacter->GetHealth() / FMath::Max(1.f, ControlledCharacter->GetMaxHealth()));
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
		if (ControlledCharacter->Melee->Active) WeaponText = TEXT("战术刀\n左键轻击 / 右键重击");
		else if (auto* Weapon = ControlledCharacter->GetEquippedWeapon())
		{
			const auto* Definition = UExtractionItemDefinition::Get(Weapon->ExtractionDefinition);
			const auto* Ammo = UExtractionItemDefinition::Get(Weapon->ExtractionAmmoType);
			WeaponText = FString::Printf(TEXT("%s\n%d / %d\n%s"),
			                             Definition ? *Definition->Label.ToString() : TEXT("步枪"),
			                             Weapon->GetAmmo(), ControlledCharacter->GetCombat()->GetCarriedAmmo(),
			                             Ammo ? *Ammo->Label.ToString() : TEXT("弹药"));
		}
		else WeaponText = TEXT("未持枪\n靠近武器按 E 拾取");
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
