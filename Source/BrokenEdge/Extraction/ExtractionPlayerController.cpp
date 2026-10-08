#include "ExtractionPlayerController.h"
#include "BrokenEdge/Traversal/TraversalMovementComponent.h"
#include "BrokenEdge/Traversal/ExtractionTraversalComponent.h"
#include "ExtractionRegionStream.h"
#include "ExtractionHealth.h"
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

void AExtractionPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AExtractionPlayerController, SpectatedPlayer, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AExtractionPlayerController, bExtractionSpectating, COND_OwnerOnly);
}

void AExtractionPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ThisClass::ToggleExtractionMenu);
	InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ThisClass::ToggleInventory);
	InputComponent->BindKey(EKeys::H, IE_Pressed, this, &ThisClass::UseMedical);
	InputComponent->BindKey(EKeys::Z, IE_Pressed, this, &ThisClass::HealAbility);
	InputComponent->BindKey(EKeys::X, IE_Pressed, this, &ThisClass::SmokeAbility);
	InputComponent->BindKey(EKeys::E, IE_Released, this, &ThisClass::ReleaseInteract);
}

void AExtractionPlayerController::ToggleExtractionMenu()
{
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

UExtractionInteractionComponent* AExtractionPlayerController::Interaction() const
{
	const auto* C = Cast<ABrokenEdgeCharacter>(GetPawn());
	return C ? C->GetExtractionInteraction() : nullptr;
}

void AExtractionPlayerController::RequestOpenInventory(AExtractionLootActor* Target, bool bOpen)
{
	auto* I = Interaction();
	if (!I) return;
	LocalInventorySession = FMath::Max(LocalInventorySession, I->Session) + 1;
	if (bOpen) I->StopCombatInput();
	ServerOpenInventory(LocalInventorySession, Target, bOpen);
}

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

void AExtractionPlayerController::ToggleInventory()
{
	if (auto* I = Interaction(); I && I->CanAct() && !bMenuOpen) RequestOpenInventory(nullptr, !I->InventoryOpen);
}

void AExtractionPlayerController::ReleaseInteract()
{
	if (auto* C = Cast<ABrokenEdgeCharacter>(GetPawn())) C->GetExtractionAbilities()->ReleaseRescue();
}
void AExtractionPlayerController::HealAbility()
{
	if (auto* C = Cast<ABrokenEdgeCharacter>(GetPawn()); C && !bMenuOpen)
		C->GetExtractionAbilities()->Request(EExtractionAction::Heal, C->GetExtractionAbilities()->HealFocus());
}
void AExtractionPlayerController::SmokeAbility()
{
	if (auto* C = Cast<ABrokenEdgeCharacter>(GetPawn()); C && !bMenuOpen) C->GetExtractionAbilities()->Request(EExtractionAction::Smoke);
}

void AExtractionPlayerController::Interact()
{
	if (auto* C = Cast<ABrokenEdgeCharacter>(GetPawn()); C && !bMenuOpen)
		if (CastChecked<UTraversalMovementComponent>(C->GetCharacterMovement())->RequestInteract()) return;
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

void AExtractionPlayerController::ServerUnlockDoor_Implementation(AExtractionKeyRoom* Door, int32 Revision)
{
	if (IsValid(Door) && Door->GetWorld() == GetWorld())
		ClientItemResult(0, Door->Unlock(Interaction(), Revision));
}

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

void AExtractionPlayerController::ServerItemOperation_Implementation(FExtractionItemRequest Request)
{
	if (auto* I = Interaction()) ClientItemResult(Request.Sequence, I->Execute(Request));
}

void AExtractionPlayerController::ClientItemResult_Implementation(uint32 Sequence, const FString& Error)
{
	ItemMessage = Error;
	ItemResultSequence = FMath::Max(ItemResultSequence, Sequence);
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_ITEM_RESULT Sequence=%u Error=%s"), Sequence, *Error);
}

void AExtractionPlayerController::ServerCancelMedical_Implementation()
{
	if (auto* I = Interaction()) I->CancelMedical();
}

void AExtractionPlayerController::ServerSwapExtractionWeapons_Implementation()
{
	if (auto* I = Interaction(); I && I->CanAct() && !I->InventoryOpen)
	{
		I->CancelMedical();
		auto* C = Cast<ABrokenEdgeCharacter>(GetPawn());
		if (C->GetCombat()->ShouldSwapWeapons()) C->GetCombat()->SwapWeapons();
	}
}

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
	ItemMessage = TEXT("没有医疗包");
}

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

void AExtractionPlayerController::Destroyed()
{
	if (HasAuthority()) if (auto* I = Interaction()) I->DropOnExit();
	Super::Destroyed();
}

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

void AExtractionPlayerController::ServerCycleSpectator_Implementation(int32 Direction)
{
	const auto* PS = GetPlayerState<AExtractionPlayerState>();
	if (bExtractionSpectating && PS && PS->Life != EExtractionLife::Alive && (Direction == 1 || Direction == -1))
		SelectSpectatorTarget(Direction);
}

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

void AExtractionPlayerController::Tick(float Delta)
{
	Super::Tick(Delta);
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
		if (const auto* Region = AExtractionRegionStream::Find(GetWorld()); Region && !Region->IsViewReady(Target->GetActorLocation())) return;
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

void AExtractionPlayerController::ClientLeaveExtraction_Implementation()
{
	DestroyLocalSession();
}

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

void AExtractionPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(LeaveRetryTimer);
	if (ExtractionWidget) ExtractionWidget->RemoveFromParent();
	if (InventoryWidget) InventoryWidget->RemoveFromParent();
	if (SpectatorCamera) SpectatorCamera->Destroy();
	if (auto* Sessions = GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>())
		Sessions->MultiplayerOnDestorySessionComplete.RemoveDynamic(this, &ThisClass::LocalSessionDestroyed);
	Super::EndPlay(Reason);
}

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
	VitalsSlot->SetSize(FVector2D(500, 70));
	return true;
}

void UExtractionWidget::Refresh()
{
	const auto* PC = GetOwningPlayer<AExtractionPlayerController>();
	const auto* GS = GetWorld()->GetGameState<AExtractionGameState>();
	const auto* PS = PC ? PC->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!PC || !GS || !PS) return;
	FVector Eye;
	FRotator View;
	PC->GetPlayerViewPoint(Eye, View);
	SmokeHaze->SetVisibility(AExtractionSmoke::Blocks(GetWorld(), Eye, Eye) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	const int32 Seconds = FMath::Max(0, FMath::CeilToInt(GS->Deadline - GS->GetServerWorldTimeSeconds()));
	FString Text;
	if (GS->Phase == EExtractionPhase::Waiting) Text = TEXT("等待其他玩家加载…");
	else if (GS->Phase == EExtractionPhase::Ended)
		Text = FString::Printf(
			TEXT("%s\n%d 秒后返回准备大厅"), *GS->EndReason, Seconds);
	else
		Text = FString::Printf(
			TEXT("航天基地 · 小队行动\n剩余 %02d:%02d · 存活 %d\n%s"), Seconds / 60, Seconds % 60, GS->AlivePlayers,
			PS->Life == EExtractionLife::Alive ? TEXT("Esc 打开对局菜单") : PS->Life == EExtractionLife::Downed ? TEXT("倒地待救援 · Esc 打开菜单") : TEXT("本次行动已结束"));
	if (!GS->StorageError.IsEmpty()) Text = GS->StorageError;
	if (PC->ExtractionAccount && PS->Life == EExtractionLife::Alive)
	{
		const auto& Progression = PC->ExtractionAccount->Profile.Progression;
		const auto* Rules = GetDefault<UExtractionProgressionRules>();
		for (int32 Q = 0; Q < Progression.Quests.Num(); ++Q)
			if (Progression.Quests[Q].Accepted && !Progression.Quests[Q].Claimed)
			{
				Text += TEXT("\n任务 · ") + Rules->Quests[Q].Label;
				if (Q == 1) Text += FString::Printf(TEXT(" %d / %d（本局 +%d，撤离保存）"), Progression.Quests[Q].Progress,
					Rules->Quests[Q].Target, PS->RaidProgress.GuardKills);
				else Text += Q == 0 ? TEXT(" · 成功撤离后完成") : TEXT(" · 文件带出后回大厅提交");
			}
	}
	if (PS->Life == EExtractionLife::Alive && GS->Phase == EExtractionPhase::Active && PC->GetPawn())
	{
		for (TActorIterator<AExtractionExit> It(GetWorld()); It; ++It)
		{
			Text += FString::Printf(
				TEXT("\n北侧撤离点 · %.0f 米"),
				FVector::Dist2D(PC->GetPawn()->GetActorLocation(), It->GetActorLocation()) / 100);
			break;
		}
		if (PS->ExtractAt > 0)
			Text += FString::Printf(
				TEXT("\n撤离倒计时 %.1f 秒 · 离开重置"), FMath::Max(0.0, PS->ExtractAt - GS->GetServerWorldTimeSeconds()));
	}
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
		if (const auto* Mate = Cast<AExtractionPlayerState>(State); Mate && Mate != PS && Mate->ExtractionSquad == PS->ExtractionSquad)
		{
			const TCHAR* Label = Mate->Life == EExtractionLife::Alive ? TEXT("存活") : Mate->Life == EExtractionLife::Downed ? TEXT("倒地")
				: Mate->Life == EExtractionLife::Extracted ? TEXT("已撤离") : TEXT("已离场");
			Text += FString::Printf(TEXT("\n%s · %s"), *Mate->GetPlayerName(), Label);
			if (auto* C = Cast<ABrokenEdgeCharacter>(Mate->GetPawn()); C && Mate->Life == EExtractionLife::Alive) Text += FString::Printf(TEXT(" · %.0f HP"), C->GetHealth());
		}
	if (PS->Life == EExtractionLife::Downed)
		Text += FString::Printf(TEXT("\n你已倒地 · %.0f 生命 · %.1f 秒后阵亡\n缓慢移动至掩体，等待队友救援"), PS->DownHealth, FMath::Max(0.0, PS->BleedAt - Now));
	if (auto* C = Cast<ABrokenEdgeCharacter>(PC->GetPawn()))
	{
		const FString TraversalPrompt = C->GetTraversal()->Prompt();
		if (!TraversalPrompt.IsEmpty()) Text += TEXT("\n") + TraversalPrompt;
		const auto* A = C->GetExtractionAbilities();
		if (PS->Life == EExtractionLife::Alive)
		{
			Text += FString::Printf(TEXT("\nZ 治疗 %.0fs · X 烟雾 %.0fs"), FMath::Max(0.0, A->HealReadyAt - Now), FMath::Max(0.0, A->SmokeReadyAt - Now));
			if (A->RescueFocus()) Text += TEXT("\n按住 E 救援队友 · 5 秒");
		}
		if (A->Action != EExtractionAction::None)
			Text += FString::Printf(TEXT("\n%s · %.1f 秒"), A->Action == EExtractionAction::Revive ? TEXT("正在救援") : A->Action == EExtractionAction::Heal ? TEXT("正在治疗") : TEXT("投放烟雾"), FMath::Max(0.0, A->ActionEnd - Now));
		if (A->Rescuer) Text += FString::Printf(TEXT("\n队友正在救援 · %.1f 秒"), FMath::Max(0.0, A->Rescuer->GetExtractionAbilities()->ActionEnd - Now));
	}
	Status->SetText(FText::FromString(Text));
	FString VitalText;
	if (auto* ControlledCharacter = Cast<ABrokenEdgeCharacter>(PC->GetPawn()))
	{
		VitalText = FString::Printf(TEXT("生命 %d / %d\n"),
		                            FMath::CeilToInt(ControlledCharacter->GetHealth()),
		                            FMath::CeilToInt(ControlledCharacter->GetMaxHealth()));
		if (const auto* Inv = ControlledCharacter->GetExtractionInteraction()->Inventory())
		{
			for (int32 ArmorSlot = 2; ArmorSlot <= 3; ++ArmorSlot)
			{
				const auto* Armor = Inv->Contents.InSlot(ArmorSlot);
				VitalText += (ArmorSlot == 2 ? TEXT("头盔 ") : TEXT("  胸甲 ")) + (Armor ? ExtractionItems::Details(*Armor) : FString(TEXT("无")));
			}
			VitalText += TEXT("\n");
		}
		const auto* Trauma = ControlledCharacter->GetExtractionHealth();
		if (Trauma->Bleeding) VitalText += TEXT("流血 · 需绷带  ");
		if (Trauma->LegFracture) VitalText += TEXT("腿部骨折 · 需夹板");
		if (Trauma->Bleeding || Trauma->LegFracture) VitalText += TEXT("\n");
		if (auto* Weapon = ControlledCharacter->GetEquippedWeapon())
			VitalText += FString::Printf(TEXT("AK · %s · %d / %d"), Weapon->ExtractionAmmoType == TEXT("AmmoAP") ? TEXT("穿甲弹") : TEXT("普通弹"), Weapon->GetAmmo(),
			                             ControlledCharacter->GetCombat()->GetCarriedAmmo());
		else VitalText += TEXT("未持枪 · 靠近武器按 E 拾取");
	}
	Vitals->SetText(FText::FromString(VitalText));
	Previous->SetVisibility(PC->bExtractionSpectating ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	Next->SetVisibility(Previous->GetVisibility());
	Previous->SetIsEnabled(PC->SpectatedPlayer != nullptr);
	Next->SetIsEnabled(PC->SpectatedPlayer != nullptr);
	Leave->SetVisibility(PC->IsMenuOpen() || PC->bExtractionSpectating || GS->Phase == EExtractionPhase::Ended
		                     ? ESlateVisibility::Visible
		                     : ESlateVisibility::Collapsed);
}

void UExtractionWidget::PreviousClicked()
{
	if (auto* PC = GetOwningPlayer<AExtractionPlayerController>()) PC->ServerCycleSpectator(-1);
}

void UExtractionWidget::NextClicked()
{
	if (auto* PC = GetOwningPlayer<AExtractionPlayerController>()) PC->ServerCycleSpectator(1);
}

void UExtractionWidget::LeaveClicked()
{
	if (auto* PC = GetOwningPlayer<AExtractionPlayerController>()) PC->ServerLeaveExtraction();
}
