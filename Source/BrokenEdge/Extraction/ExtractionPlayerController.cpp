#include "ExtractionPlayerController.h"
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
}

void AExtractionPlayerController::ToggleExtractionMenu() { bMenuOpen = !bMenuOpen; }

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
	TArray<AExtractionPlayerState*> Targets;
	for (APlayerState* State : GS->PlayerArray)
		if (auto* PS = Cast<AExtractionPlayerState>(State); PS && PS->Life == EExtractionLife::Alive && PS->GetPawn() &&
			!PS->IsInactive())
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
	if (bExtractionSpectating && PS && PS->Life == EExtractionLife::Dead && (Direction == 1 || Direction == -1))
		SelectSpectatorTarget(Direction);
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
	if (auto* ControlledCharacter = Cast<ABrokenEdgeCharacter>(GetPawn()))
	{
		// The owning client also stops prediction while waiting for all players to load.
		if (GS->Phase != EExtractionPhase::Active || ControlledCharacter->IsDefeated())
			ControlledCharacter->GetCharacterMovement()->DisableMovement();
		else if (ControlledCharacter->GetCharacterMovement()->MovementMode == MOVE_None)
			ControlledCharacter->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
	const bool ShowMenu = bMenuOpen || bExtractionSpectating || GS->Phase == EExtractionPhase::Ended;
	if (ShowMenu != bShowMouseCursor)
	{
		SetShowMouseCursor(ShowMenu);
		if (ShowMenu) SetInputMode(FInputModeGameAndUI());
		else SetInputMode(FInputModeGameOnly());
	}
	ExtractionWidget->Refresh();
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
	auto* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrushColor(FLinearColor(0.015f, 0.025f, 0.04f, .85f));
	Panel->SetPadding(FMargin(14));
	auto* PanelSlot = Canvas->AddChildToCanvas(Panel);
	PanelSlot->SetPosition(FVector2D(18, 18));
	PanelSlot->SetSize(FVector2D(360, 220));
	auto* Box = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->AddChild(Box);
	Status = SessionWidgetStyle::Text(WidgetTree, TEXT("正在加载航天基地…"), 16);
	Status->SetAutoWrapText(true);
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
	const int32 Seconds = FMath::Max(0, FMath::CeilToInt(GS->Deadline - GS->GetServerWorldTimeSeconds()));
	FString Text;
	if (GS->Phase == EExtractionPhase::Waiting) Text = TEXT("等待其他玩家加载…");
	else if (GS->Phase == EExtractionPhase::Ended)
		Text = FString::Printf(
			TEXT("%s\n%d 秒后返回准备大厅"), *GS->EndReason, Seconds);
	else
		Text = FString::Printf(
			TEXT("航天基地 · 战斗测试\n剩余 %02d:%02d · 存活 %d\n%s"), Seconds / 60, Seconds % 60, GS->AlivePlayers,
			PS->Life == EExtractionLife::Alive ? TEXT("Esc 打开对局菜单") : TEXT("已阵亡 · 本局不会重生"));
	if (PC->bExtractionSpectating)
		Text += PC->SpectatedPlayer ? TEXT("\n正在观战：") + PC->SpectatedPlayer->GetPlayerName() : TEXT("\n暂无存活玩家可供观战");
	Status->SetText(FText::FromString(Text));
	FString VitalText;
	if (auto* ControlledCharacter = Cast<ABrokenEdgeCharacter>(PC->GetPawn()))
	{
		VitalText = FString::Printf(TEXT("生命 %d / %d   护盾 %d\n"),
		                            FMath::CeilToInt(ControlledCharacter->GetHealth()),
		                            FMath::CeilToInt(ControlledCharacter->GetMaxHealth()),
		                            FMath::CeilToInt(ControlledCharacter->GetShield()));
		if (auto* Weapon = ControlledCharacter->GetEquippedWeapon())
			VitalText += FString::Printf(TEXT("AK · %d / %d"), Weapon->GetAmmo(),
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
