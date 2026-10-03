#include "ExtractionGameMode.h"
#include "ExtractionState.h"
#include "ExtractionPlayerController.h"
#include "ExtractionCombatComponent.h"
#include "ExtractionInteractionComponent.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionAccountComponent.h"
#include "ExtractionExit.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/UI/BrokenEdgeHUD.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/GameSession.h"
#include "Kismet/GameplayStatics.h"
#include "MultiplayerSessionsSubsystem.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/FileHelper.h"
#include "HAL/PlatformProcess.h"
#endif

AExtractionGameMode::AExtractionGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	bDelayedStart = true;
	bUseSeamlessTravel = true;
	GameStateClass = AExtractionGameState::StaticClass();
	PlayerStateClass = AExtractionPlayerState::StaticClass();
	PlayerControllerClass = AExtractionPlayerController::StaticClass();
	static ConstructorHelpers::FClassFinder<APawn>
		Pawn(TEXT("/Game/Characters/Stinger/Blueprints/BP_StingerCharacter"));
	if (Pawn.Succeeded()) DefaultPawnClass = Pawn.Class;
	HUDClass = ABrokenEdgeHUD::StaticClass();
	for (FName Id : UExtractionItemDefinition::Ids())
		if (const auto* Definition = UExtractionItemDefinition::Get(Id))
			ItemDefinitions.Add(const_cast<UExtractionItemDefinition*>(Definition));
}

void AExtractionGameMode::InitGame(const FString& Map, const FString& Options, FString& Error)
{
	Super::InitGame(Map, Options, Error);
	ExpectedPlayers = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("ExpectedPlayers"), 2), 1, 8);
	float Duration = 600;
	FParse::Value(FCommandLine::Get(), TEXT("ExtractionRoundSeconds="), Duration);
	RoundSeconds = FMath::Clamp(static_cast<double>(Duration), 10.0, 3600.0);
}

void AExtractionGameMode::BeginPlay()
{
	Super::BeginPlay();
	LoadedAt = GetWorld()->GetTimeSeconds();
	if (auto* Persistence = Store()) Persistence->EnsureLoaded();
	FHitResult Floor;
	// Start below the overhead mesh, inside the verified north-exit walking space.
	const FVector Above(1400, 2900, 650), Below(1400, 2900, -1000);
	const bool Hit = GetWorld()->LineTraceSingleByChannel(Floor, Above, Below, ECC_Visibility);
	Exit = GetWorld()->SpawnActor<AExtractionExit>(FVector(1400, 2900, Hit ? Floor.ImpactPoint.Z + 3 : 365),
	                                               FRotator::ZeroRotator);
	if (auto* Instance = GetGameInstance())
		if (auto* Sessions = Instance->GetSubsystem<UMultiplayerSessionsSubsystem>())
			Sessions->MultiplayerOnEndSessionComplete.AddUniqueDynamic(this, &ThisClass::SessionEnded);
}

void AExtractionGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& Id,
                                   FString& Error)
{
	Super::PreLogin(Options, Address, Id, Error);
	const auto* State = GetGameState<AExtractionGameState>();
	if (State && State->Phase != EExtractionPhase::Waiting) Error = TEXT("MATCH_IN_PROGRESS");
}

void AExtractionGameMode::HandleStartingNewPlayer_Implementation(APlayerController* Player)
{
	const auto* State = GetGameState<AExtractionGameState>();
	if (!Player || !State || State->Phase != EExtractionPhase::Waiting) return;
	if (auto* PS = Player->GetPlayerState<AExtractionPlayerState>())
	{
		PS->Life = EExtractionLife::Alive;
		PS->SetLobbyReady(false);
		PS->SetTeam(ETeam::ET_NoTeam);
	}
	if (!Player->GetPawn()) RestartPlayer(Player);
	if (auto* PS = Player->GetPlayerState<AExtractionPlayerState>()) PS->Inventory->InitializeLoadout();
	FreezePlayer(Player, true);
	if (auto* Character = Cast<ABrokenEdgeCharacter>(Player->GetPawn()))
		Character->GetMesh()->VisibilityBasedAnimTickOption =
			EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}

bool AExtractionGameMode::PlayerCanRestart_Implementation(APlayerController* Player)
{
	const auto* GS = GetGameState<AExtractionGameState>();
	const auto* PS = Player ? Player->GetPlayerState<AExtractionPlayerState>() : nullptr;
	return GS && GS->Phase == EExtractionPhase::Waiting && PS && PS->Life == EExtractionLife::Alive && !Player->
		GetPawn();
}

AActor* AExtractionGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	TArray<AActor*> Starts;
	UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), Starts);
	Starts.Sort([](const AActor& A, const AActor& B)
	{
		return CastChecked<APlayerStart>(&A)->PlayerStartTag.LexicalLess(CastChecked<APlayerStart>(&B)->PlayerStartTag);
	});
	for (AActor* Start : Starts)
	{
		if (Start->ActorHasTag(TEXT("ExtractionSpawn")) && !ClaimedStarts.Contains(Start))
		{
			ClaimedStarts.Add(Start);
			return Start;
		}
	}
	return nullptr;
}

void AExtractionGameMode::FreezePlayer(AController* Player, bool bFreeze)
{
	if (auto* Character = Player ? Cast<ABrokenEdgeCharacter>(Player->GetPawn()) : nullptr)
	{
		Character->bDisableGameplay = bFreeze;
		if (bFreeze)
		{
			Character->GetCharacterMovement()->StopMovementImmediately();
			Character->GetCharacterMovement()->DisableMovement();
			Character->GetCombat()->FireButtonPressed(false);
			Character->GetExtractionCombat()->CancelReload();
			Character->GetExtractionCombat()->CancelSwap();
			Character->GetExtractionInteraction()->Close();
			Character->GetExtractionInteraction()->CancelMedical();
		}
		else Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Character->ForceNetUpdate();
	}
}

bool AExtractionGameMode::IsCombatActive(AController* Player) const
{
	const auto* GS = GetGameState<AExtractionGameState>();
	const auto* PS = Player ? Player->GetPlayerState<AExtractionPlayerState>() : nullptr;
	const auto* Persistence = Store();
	return GS && GS->Phase == EExtractionPhase::Active && PS && PS->Life == EExtractionLife::Alive &&
		Persistence && Persistence->IsAvailable();
}

UExtractionPersistence* AExtractionGameMode::Store() const
{
	const auto* World = GetWorld();
	if (bEndingPlay || !World || World->bIsTearingDown) return nullptr;
	auto* Instance = GetGameInstance();
	return Instance ? Instance->GetSubsystem<UExtractionPersistence>() : nullptr;
}

bool AExtractionGameMode::SettlePlayer(AController* Player, EExtractionOutcome Outcome)
{
	auto* Persistence = Store();
	if (!Persistence || !Persistence->IsAvailable()) return false;
	auto* PS = Player ? Player->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!PS) return false;
	if (PS->Life != EExtractionLife::Alive) return true;
	if (!PS->Inventory || !PS->ExtractionRaid.IsValid() || !PS->ExtractionProfile.IsValid()) return false;
	PS->Inventory->CaptureWeapons();
	if (!Persistence->Settle(PS->ExtractionRaid, PS->ExtractionProfile, Outcome, PS->Inventory->Contents)) return false;
#if WITH_DEV_AUTOMATION_TESTS
	FString HoldFile;
	if (Outcome == EExtractionOutcome::Extracted && FParse::Value(FCommandLine::Get(),
	                                                              TEXT("ExtractionHoldSettlement="), HoldFile))
	{
		// External recovery test terminates this owned server after durable commit, before any result acknowledgement.
		FFileHelper::SaveStringToFile(TEXT("committed-before-ack"), *HoldFile);
		for (;;) FPlatformProcess::Sleep(1);
	}
#endif
	PS->ExtractAt = 0;
	if (auto* Account = Player->FindComponentByClass<UExtractionAccountComponent>()) Account->Refresh();
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_SETTLED Profile=%s Outcome=%d"), *PS->ExtractionProfile.ToString(),
	       static_cast<int32>(Outcome));
	return true;
}

void AExtractionGameMode::UpdateExtraction(double Now)
{
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		auto* PC = Cast<AExtractionPlayerController>(It->Get());
		auto* PS = PC ? PC->GetPlayerState<AExtractionPlayerState>() : nullptr;
		auto* C = PC ? Cast<ABrokenEdgeCharacter>(PC->GetPawn()) : nullptr;
		if (!PS || PS->Life != EExtractionLife::Alive || !C || C->IsDefeated()) continue;
		if (!Exit || !Exit->Contains(C->GetActorLocation()))
		{
			PS->ExtractAt = 0;
			continue;
		}
		if (PS->ExtractAt <= 0)
		{
			PS->ExtractAt = Now + 10;
			PS->ForceNetUpdate();
		}
		if (Now < PS->ExtractAt) continue;
		FreezePlayer(PC, true);
		if (!SettlePlayer(PC, EExtractionOutcome::Extracted)) continue;
		PS->Life = EExtractionLife::Extracted;
		PS->ForceNetUpdate();
		PS->Inventory->ClearSettled();
		PC->BeginExtractionSpectating();
		C->Destroy();
	}
}

float AExtractionGameMode::CalculateDamage(AController* Attacker, AController* Victim, float Damage)
{
	if (!IsCombatActive(Victim) || (Attacker && !IsCombatActive(Attacker)) || !FMath::IsFinite(Damage)) return 0;
	return FMath::Max(0.f, Damage);
}

void AExtractionGameMode::RefreshAliveCount()
{
	auto* GS = GetGameState<AExtractionGameState>();
	if (!GS) return;
	int32 Count = 0;
	for (APlayerState* State : GS->PlayerArray)
		if (const auto* PS = Cast<AExtractionPlayerState>(State); PS && PS->Life == EExtractionLife::Alive && !PS->
			IsInactive())
			++Count;
	GS->AlivePlayers = Count;
}

void AExtractionGameMode::Tick(float Delta)
{
	Super::Tick(Delta);
	if (bEndingPlay || !GetWorld() || GetWorld()->bIsTearingDown) return;
	auto* GS = GetGameState<AExtractionGameState>();
	if (!GS) return;
	const double Now = GetWorld()->GetTimeSeconds();
	auto* Persistence = Store();
	if (!Persistence || !Persistence->IsAvailable())
	{
		GS->StorageError = Persistence ? Persistence->Error : TEXT("仓库服务不可用");
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It) FreezePlayer(It->Get(), true);
		return;
	}
	RefreshAliveCount();
	if (GS->Phase == EExtractionPhase::Waiting)
	{
		int32 Ready = 0;
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (It->Get()->HasClientLoadedCurrentWorld() && It->Get()->GetPawn()) ++Ready;
		if (Ready >= ExpectedPlayers)
		{
			FGuid Raid;
			bool ValidTickets = true;
			TSet<FGuid> Profiles;
			for (APlayerState* State : GS->PlayerArray)
			{
				const auto* PS = Cast<AExtractionPlayerState>(State);
				if (!PS || !PS->ExtractionRaid.IsValid() || !PS->ExtractionProfile.IsValid())
				{
					ValidTickets = false;
					continue;
				}
				if (!Raid.IsValid()) Raid = PS->ExtractionRaid;
				ValidTickets &= Raid == PS->ExtractionRaid && !Profiles.Contains(PS->ExtractionProfile);
				Profiles.Add(PS->ExtractionProfile);
			}
			const auto* Record = Persistence->FindRaid(Raid);
			ValidTickets &= Record && !Record->Closed && Record->Players.Num() == Profiles.Num();
			if (Record) for (const auto& Member : Record->Players)
				ValidTickets &= Profiles.Contains(Member.Profile) && Member.Outcome == EExtractionOutcome::None;
			if (!ValidTickets || !Raid.IsValid())
			{
				FinishRound(TEXT("请从大厅准备后进入对局"));
				return;
			}
			if (!Persistence->ActivateRaid(Raid))
			{
				if (Persistence->IsAvailable()) FinishRound(TEXT("出战记录已取消"));
				return;
			}
			// A prepared lobby ticket is the only valid way to enter an active raid.
			GS->Phase = EExtractionPhase::Active;
			GS->Deadline = Now + RoundSeconds;
			StartMatch();
			for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It) FreezePlayer(It->Get(), false);
			UE_LOG(LogTemp, Display, TEXT("EXTRACTION_ACTIVE Players=%d Duration=%.1f"), Ready, RoundSeconds);
		}
		else if (Now - LoadedAt >= 120) FinishRound(TEXT("玩家加载超时"));
	}
	else if (GS->Phase == EExtractionPhase::Active)
	{
		if (GS->AlivePlayers == 0) FinishRound(TEXT("无人存活"));
		else if (Now >= GS->Deadline) FinishRound(TEXT("本轮时间结束"));
		else UpdateExtraction(Now);
	}
	else if (!bTravelRequested && !bEndRequested && Now >= ReturnAt)
	{
		auto* Instance = GetGameInstance();
		auto* Sessions = Instance ? Instance->GetSubsystem<UMultiplayerSessionsSubsystem>() : nullptr;
		if (Sessions && Sessions->IsBusy()) return;
		bEndRequested = true;
		if (Sessions && Sessions->HasSession()) Sessions->EndSession();
		else SessionEnded(true);
	}
}

void AExtractionGameMode::FinishRound(const FString& Reason)
{
	auto* Persistence = Store();
	if (!Persistence || !Persistence->IsAvailable()) return;
	auto* GS = GetGameState<AExtractionGameState>();
	if (!GS || GS->Phase == EExtractionPhase::Ended) return;
	if (GS->Phase == EExtractionPhase::Waiting)
	{
		// A failed hard travel can lose every PlayerState ticket. The server store still owns the records.
		if (!Persistence->AbortPreparedRaids()) return;
	}
	else
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			auto* PS = It->Get()->GetPlayerState<AExtractionPlayerState>();
			if (!PS || PS->Life != EExtractionLife::Alive) continue;
			if (!SettlePlayer(It->Get(), EExtractionOutcome::TimedOut)) return;
			PS->Life = EExtractionLife::TimedOut;
			PS->Inventory->ClearSettled();
		}
	GS->Phase = EExtractionPhase::Ended;
	GS->EndReason = Reason;
	ReturnAt = GetWorld()->GetTimeSeconds() + 10;
	GS->Deadline = ReturnAt;
	GS->ForceNetUpdate();
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It) FreezePlayer(It->Get(), true);
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_ENDED Reason=%s"), *Reason);
}

void AExtractionGameMode::SessionEnded(bool bSuccess)
{
	if (bEndingPlay || !GetWorld() || GetWorld()->bIsTearingDown) return;
	bEndRequested = false;
	if (bTravelRequested) return;
	if (!bSuccess)
	{
		ReturnAt = GetWorld()->GetTimeSeconds() + 2;
		return;
	}
	const int32 Capacity = GameSession ? GameSession->MaxPlayers : 2;
	bTravelRequested = GetWorld()->ServerTravel(FString::Printf(TEXT("/Game/Maps/Lobby?MaxPlayers=%d"), Capacity));
	if (!bTravelRequested) ReturnAt = GetWorld()->GetTimeSeconds() + 2;
}

void AExtractionGameMode::PlayerDefeated(ABrokenEdgeCharacter* Character, ABrokenEdgePlayerController* Victim,
                                         ABrokenEdgePlayerController* Attacker)
{
	auto* PS = Victim ? Victim->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!PS || PS->Life != EExtractionLife::Alive) return;
	if (!SettlePlayer(Victim, EExtractionOutcome::Dead)) return;
	PS->Life = EExtractionLife::Dead;
	PS->ForceNetUpdate();
	if (Character) Character->GetExtractionCombat()->CancelReload();
	Super::PlayerDefeated(Character, Victim, Attacker);
	RefreshAliveCount();
}

void AExtractionGameMode::RequestRespawn(ACharacter* Character, AController* Controller)
{
	auto* PC = Cast<AExtractionPlayerController>(Controller);
	const auto* PS = PC ? PC->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!PS || PS->Life == EExtractionLife::Alive) return;
	PC->BeginExtractionSpectating();
	if (Character) Character->Destroy();
}

void AExtractionGameMode::LeavePlayer(AController* Player)
{
	if (bEndingPlay || !GetWorld() || GetWorld()->bIsTearingDown) return;
	auto* PS = Player ? Player->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!PS || PS->Life != EExtractionLife::Alive) return;
	const auto* GS = GetGameState<AExtractionGameState>();
	// Direct-map PIE has no lobby ticket and must never create a settlement.
	if (!PS->ExtractionRaid.IsValid())
	{
		if (PS->Inventory) PS->Inventory->ClearSettled();
	}
	else if (GS && GS->Phase == EExtractionPhase::Waiting)
	{
		auto* Persistence = Store();
		if (!Persistence || !PS->Inventory || !Persistence->AbortRaid(PS->ExtractionRaid)) return;
		PS->Inventory->ClearSettled();
	}
	else if (!SettlePlayer(Player, EExtractionOutcome::Left)) return;
	PS->Life = EExtractionLife::Left;
	PS->ForceNetUpdate();
	if (auto* Character = Cast<ABrokenEdgeCharacter>(Player->GetPawn()); Character && !Character->IsDefeated())
	{
		Character->GetExtractionCombat()->CancelReload();
		Character->Defeated(true);
	}
	RefreshAliveCount();
}

void AExtractionGameMode::Logout(AController* Player)
{
	// PIE deinitializes GameInstance subsystems before the net driver destroys its controllers.
	if (!bEndingPlay && GetWorld() && !GetWorld()->bIsTearingDown)
	{
		const auto* GS = GetGameState<AExtractionGameState>();
		if (auto* PS = Player ? Player->GetPlayerState<AExtractionPlayerState>() : nullptr;
			PS && PS->Life == EExtractionLife::Alive && GS && GS->Phase == EExtractionPhase::Active)
			SettlePlayer(Player, EExtractionOutcome::Disconnected);
		LeavePlayer(Player);
	}
	Super::Logout(Player);
}

void AExtractionGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	bEndingPlay = true;
	if (auto* Instance = GetGameInstance())
		if (auto* Sessions = Instance->GetSubsystem<UMultiplayerSessionsSubsystem>())
			Sessions->MultiplayerOnEndSessionComplete.RemoveDynamic(this, &ThisClass::SessionEnded);
	Super::EndPlay(Reason);
}
