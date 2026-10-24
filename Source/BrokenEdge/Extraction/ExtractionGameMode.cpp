#include "ExtractionGameMode.h"
#include "ExtractionLootLayout.h"
#include "ExtractionRegionStream.h"
#include "ExtractionKeyRoom.h"
#include "ExtractionState.h"
#include "ExtractionPlayerController.h"
#include "ExtractionCombatComponent.h"
#include "ExtractionInteractionComponent.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionAccountComponent.h"
#include "ExtractionExit.h"
#include "ExtractionAbilities.h"
#include "ExtractionGuard.h"
#include "ExtractionSmoke.h"
#include "EngineUtils.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/UI/BrokenEdgeHUD.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Components/CapsuleComponent.h"
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
	ExpectedPlayers = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("ExpectedPlayers"), 2), 1, 6);
	float Duration = 600;
	FParse::Value(FCommandLine::Get(), TEXT("ExtractionRoundSeconds="), Duration);
	RoundSeconds = FMath::Clamp(static_cast<double>(Duration), 10.0, 3600.0);
	int32 Seed = FMath::Rand();
	FParse::Value(FCommandLine::Get(), TEXT("ExtractionSpawnSeed="), Seed);
	SpawnRandom.Initialize(Seed);
	LoadedMapDefinition = MapDefinition.LoadSynchronous();
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_SPAWN_SEED %d"), Seed);
}

void AExtractionGameMode::BeginPlay()
{
	Super::BeginPlay();
	LoadedAt = GetWorld()->GetTimeSeconds();
	if (auto* Persistence = Store()) Persistence->EnsureLoaded();
	LoadedMapDefinition = MapDefinition.LoadSynchronous();
	if (LoadedMapDefinition)
	{
		TSet<FName> Ids;
		if (LoadedMapDefinition->LootLayout)
		{
			int32 LootSeed = FMath::Rand();
			FParse::Value(FCommandLine::Get(), TEXT("ExtractionLootSeed="), LootSeed);
			LoadedMapDefinition->LootLayout->Spawn(GetWorld(), LootSeed);
		}
		for (const auto& Definition : LoadedMapDefinition->Exits)
		{
			if (!Definition.IsValid() || Ids.Contains(Definition.Id))
			{
				UE_LOG(LogTemp, Error, TEXT("EXTRACTION_INVALID_EXIT %s"), *Definition.Id.ToString());
				continue;
			}
			Ids.Add(Definition.Id);
			if (auto* NewExit = GetWorld()->SpawnActor<AExtractionExit>())
			{
				NewExit->Configure(Definition);
				Exits.Add(NewExit);
			}
		}
	}
	if (Exits.IsEmpty())
	{
		if (auto* GS = GetGameState<AExtractionGameState>()) GS->StorageError = TEXT("地图撤离配置缺失或无效，无法开始对局");
		UE_LOG(LogTemp, Error, TEXT("EXTRACTION_MAP_CONFIGURATION_INVALID"));
	}
	bMapConfigurationValid = LoadedMapDefinition && Exits.Num() == LoadedMapDefinition->Exits.Num()
		&& !Exits.IsEmpty() && LoadedMapDefinition->SpawnGroups.Num() >= 2;
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
	if (!Player->GetPawn()) return;
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

AActor* AExtractionGameMode::FindPlayerStart_Implementation(AController* Player, const FString& IncomingName)
{
	// GameModeBase otherwise falls back to WorldSettings at the world origin.
	return ChoosePlayerStart(Player);
}

AActor* AExtractionGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (const auto* Existing = PlayerStarts.Find(Player); Existing && Existing->IsValid()) return Existing->Get();
	if (const auto* Region = AExtractionRegionStream::Find(GetWorld()); Region && !Region->IsReady()) return nullptr;
	const auto* PS = Player ? Player->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!LoadedMapDefinition || !PS || PS->ExtractionSquad == 0) return nullptr;
	const auto* Pawn = DefaultPawnClass ? Cast<ACharacter>(DefaultPawnClass->GetDefaultObject()) : nullptr;
	if (!Pawn) return nullptr;
	const float Radius = Pawn->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float Half = Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const auto Ground = [&](const FTransform& Position, FTransform& Resolved)
	{
		if (Position.ContainsNaN()) return false;
		const FVector Center = Position.GetLocation();
		FHitResult Floor;
		if (!GetWorld()->LineTraceSingleByChannel(Floor, Center,
		                                          Center - FVector(0, 0, Half + 500),
		                                          ECC_Visibility) || Floor.ImpactNormal.Z < .7f)
			return false;
		Resolved = Position;
		Resolved.SetLocation(Floor.ImpactPoint + FVector(0, 0, Half + 3));
		return !GetWorld()->OverlapBlockingTestByChannel(Resolved.GetLocation(), FQuat::Identity, ECC_Pawn,
		                                                 FCollisionShape::MakeCapsule(Radius, Half));
	};
	int32* Assigned = SquadSpawnGroups.Find(PS->ExtractionSquad);
	if (!Assigned)
	{
		TArray<int32> Candidates;
		for (int32 I = 0; I < LoadedMapDefinition->SpawnGroups.Num(); ++I)
		{
			const auto& Group = LoadedMapDefinition->SpawnGroups[I];
			if (ClaimedSpawnGroups.Contains(I) || Group.Id.IsNone() || Group.Positions.Num() < 3) continue;
			bool Valid = true;
			FTransform Resolved;
			for (const auto& Position : Group.Positions) Valid &= Ground(Position, Resolved);
			for (int32 A = 0; A < Group.Positions.Num(); ++A)
				for (int32 B = A + 1; B < Group.Positions.Num(); ++B)
					Valid &= FVector::Dist2D(Group.Positions[A].GetLocation(), Group.Positions[B].GetLocation()) >
						Radius * 2;
			if (Valid) Candidates.Add(I);
		}
		if (Candidates.IsEmpty())
		{
			if (auto* GS = GetGameState<AExtractionGameState>()) GS->StorageError = TEXT("没有已加载且可站立的出生组");
			return nullptr;
		}
		const int32 Index = Candidates[SpawnRandom.RandRange(0, Candidates.Num() - 1)];
		ClaimedSpawnGroups.Add(Index);
		Assigned = &SquadSpawnGroups.Add(PS->ExtractionSquad, Index);
	}
	const auto& Group = LoadedMapDefinition->SpawnGroups[*Assigned];
	int32& Slot = SquadSpawnSlots.FindOrAdd(PS->ExtractionSquad);
	FTransform Resolved;
	if (!Group.Positions.IsValidIndex(Slot) || !Ground(Group.Positions[Slot], Resolved)) return nullptr;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Start = GetWorld()->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), Resolved, Params);
	if (Start)
	{
		Start->PlayerStartTag = Group.Id;
		ClaimedStarts.Add(Start);
		PlayerStarts.Add(Player, Start);
		++Slot;
	}
	return Start;
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
			Character->GetExtractionAbilities()->Damaged();
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
	if (PS->Life != EExtractionLife::Alive && PS->Life != EExtractionLife::Downed) return true;
	if (!PS->Inventory || !PS->ExtractionRaid.IsValid() || !PS->ExtractionProfile.IsValid()) return false;
	PS->Inventory->CaptureWeapons();
	auto Progress = PS->RaidProgress;
	for (TActorIterator<AExtractionKeyRoom> It(GetWorld()); It; ++It) Progress.FileLots.Append(It->FileLots);
	if (!Persistence->Settle(PS->ExtractionRaid, PS->ExtractionProfile, Outcome, PS->Inventory->Contents, Progress))
		return false;
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
		AExtractionExit* Exit = nullptr;
		for (const auto& Candidate : Exits)
			if (IsValid(Candidate) && Candidate->Available(Now) && Candidate->Contains(C->GetActorLocation()))
			{
				if (!Exit || Candidate->Definition.Id == PS->ExtractExitId) Exit = Candidate;
			}
		if (!Exit)
		{
			PS->ExtractAt = 0;
			PS->ExtractExitId = NAME_None;
			continue;
		}
		if (PS->ExtractAt <= 0 || PS->ExtractExitId != Exit->Definition.Id)
		{
			PS->ExtractExitId = Exit->Definition.Id;
			PS->ExtractAt = Now + Exit->Definition.HoldSeconds;
			PS->ForceNetUpdate();
		}
		if (Now < PS->ExtractAt) continue;
		FreezePlayer(PC, true);
		if (!SettlePlayer(PC, EExtractionOutcome::Extracted)) continue;
		Exit->Consume();
		PS->ExtractExitId = NAME_None;
		PS->Life = EExtractionLife::Extracted;
		PS->ForceNetUpdate();
		PS->Inventory->ClearSettled();
		PC->BeginExtractionSpectating();
		C->Destroy();
	}
}

float AExtractionGameMode::CalculateDamage(AController* Attacker, AController* Victim, float Damage)
{
	const auto* GS = GetGameState<AExtractionGameState>();
	if (!GS || GS->Phase != EExtractionPhase::Active || !Store() || !Store()->IsAvailable() || !FMath::IsFinite(Damage))
		return 0;
	const auto Valid = [](AController* Controller, bool Attacking)
	{
		const auto* C = Controller ? Cast<ABrokenEdgeCharacter>(Controller->GetPawn()) : nullptr;
		if (!C || C->IsDefeated() || C->bDisableGameplay) return false;
		if (C->bExtractionGuard) return true;
		const auto* PS = Controller->GetPlayerState<AExtractionPlayerState>();
		return PS && (PS->Life == EExtractionLife::Alive || (!Attacking && PS->Life == EExtractionLife::Downed));
	};
	if (!Valid(Victim, false) || (Attacker && !Valid(Attacker, true))) return 0;
	const auto* A = Attacker ? Cast<ABrokenEdgeCharacter>(Attacker->GetPawn()) : nullptr;
	const auto* B = Victim ? Cast<ABrokenEdgeCharacter>(Victim->GetPawn()) : nullptr;
	if (A && B && (UExtractionAbilities::Friendly(A, B) || (A->bExtractionGuard && B->bExtractionGuard))) return 0;
	return FMath::Max(0.f, Damage);
}

void AExtractionGameMode::RefreshAliveCount()
{
	auto* GS = GetGameState<AExtractionGameState>();
	if (!GS) return;
	int32 Count = 0;
	for (APlayerState* State : GS->PlayerArray)
		if (const auto* PS = Cast<AExtractionPlayerState>(State); PS && (PS->Life == EExtractionLife::Alive || PS->Life
				== EExtractionLife::Downed) && !PS->
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
	if (!bMapConfigurationValid)
	{
		GS->StorageError = TEXT("地图出生或撤离配置无效，无法开始对局");
		return;
	}
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
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (It->Get()->HasClientLoadedCurrentWorld() && !It->Get()->GetPawn())
				HandleStartingNewPlayer(It->Get());
		int32 Ready = 0;
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (It->Get()->HasClientLoadedCurrentWorld() && It->Get()->GetPawn()) ++Ready;
		const auto* Region = AExtractionRegionStream::Find(GetWorld());
		if (Ready >= ExpectedPlayers && (!Region || Region->IsReady()))
		{
			if (GS->StorageError == TEXT("没有已加载且可站立的出生组")) GS->StorageError.Reset();
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
			if (Record)
				for (const auto& Member : Record->Players)
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
			for (const auto& Exit : Exits) if (IsValid(Exit)) Exit->StartRound(Now);
			GS->Deadline = Now + RoundSeconds;
			StartMatch();
			SpawnGuards();
			for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It) FreezePlayer(It->Get(), false);
			UE_LOG(LogTemp, Display, TEXT("EXTRACTION_ACTIVE Players=%d Duration=%.1f"), Ready, RoundSeconds);
		}
		else if (Now - LoadedAt >= 120) FinishRound(TEXT("玩家加载超时"));
	}
	else if (GS->Phase == EExtractionPhase::Active)
	{
		UpdateDowned(Now);
		RefreshAliveCount();
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
			if (!PS || (PS->Life != EExtractionLife::Alive && PS->Life != EExtractionLife::Downed)) continue;
			if (!SettlePlayer(It->Get(), EExtractionOutcome::TimedOut)) return;
			PS->Life = EExtractionLife::TimedOut;
			PS->Inventory->ClearSettled();
		}
	RemoveGuards();
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
	DefeatCombatant(Character, Attacker);
}

bool AExtractionGameMode::HasRescuer(const AExtractionPlayerState* Player) const
{
	const auto* GS = GetGameState<AExtractionGameState>();
	if (!GS || !Player || !Player->ExtractionSquad) return false;
	for (const APlayerState* State : GS->PlayerArray)
		if (const auto* PS = Cast<AExtractionPlayerState>(State); PS && PS != Player && PS->ExtractionSquad == Player->
			ExtractionSquad
			&& PS->Life == EExtractionLife::Alive && !PS->IsInactive() && PS->GetPawn())
			return true;
	return false;
}

bool AExtractionGameMode::DamageDowned(ABrokenEdgeCharacter* C, float Damage)
{
	auto* PS = C ? C->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!PS || PS->Life != EExtractionLife::Downed) return false;
	PS->DownHealth = FMath::Max(0.f, PS->DownHealth - Damage);
	PS->ForceNetUpdate();
	if (PS->DownHealth <= 0) DefeatCombatant(C, nullptr, true);
	return true;
}

void AExtractionGameMode::DefeatCombatant(ABrokenEdgeCharacter* C, AController* Attacker, bool bFinal)
{
	if (!C || C->IsDefeated()) return;
	C->GetExtractionAbilities()->Damaged();
	C->GetExtractionCombat()->CancelReload();
	C->GetExtractionCombat()->CancelSwap();
	C->GetExtractionInteraction()->Close();
	C->GetExtractionInteraction()->CancelMedical();
	C->GetCombat()->FireButtonPressed(false);
	C->GetExtractionInteraction()->StopCombatInput();
	if (C->bExtractionGuard)
	{
		if (auto* Killer = Attacker ? Attacker->GetPlayerState<AExtractionPlayerState>() : nullptr;
			Killer && (Killer->Life == EExtractionLife::Alive || Killer->Life == EExtractionLife::Downed))
		{
			++Killer->RaidProgress.GuardKills;
			Killer->ForceNetUpdate();
		}
		C->Defeated(false);
		C->SetLifeSpan(10);
		return;
	}
	auto* PC = Cast<ABrokenEdgePlayerController>(C->GetController());
	auto* PS = C->GetPlayerState<AExtractionPlayerState>();
	if (!PS || (PS->Life != EExtractionLife::Alive && PS->Life != EExtractionLife::Downed)) return;
	if (!bFinal && PS->Life == EExtractionLife::Alive && HasRescuer(PS))
	{
		PS->Life = EExtractionLife::Downed;
		PS->DownHealth = GetDefault<UExtractionRules>()->DownHealth;
		PS->BleedAt = GetWorld()->GetTimeSeconds() + GetDefault<UExtractionRules>()->BleedSeconds;
		PS->ExtractAt = 0;
		PS->ForceNetUpdate();
		C->Crouch();
		C->ForceNetUpdate();
		return;
	}
	if (!SettlePlayer(PC, EExtractionOutcome::Dead)) return;
	PS->Life = EExtractionLife::Dead;
	PS->BleedAt = 0;
	PS->DownHealth = 0;
	PS->ForceNetUpdate();
	Super::PlayerDefeated(C, PC, Cast<ABrokenEdgePlayerController>(Attacker));
	RefreshAliveCount();
}

void AExtractionGameMode::UpdateDowned(double Now)
{
	for (APlayerState* State : GetGameState<AExtractionGameState>()->PlayerArray)
		if (auto* PS = Cast<AExtractionPlayerState>(State); PS && PS->Life == EExtractionLife::Downed
			&& (Now >= PS->BleedAt || !HasRescuer(PS)))
			DefeatCombatant(Cast<ABrokenEdgeCharacter>(PS->GetPawn()), nullptr, true);
}

void AExtractionGameMode::RequestRespawn(ACharacter* Character, AController* Controller)
{
	auto* PC = Cast<AExtractionPlayerController>(Controller);
	const auto* PS = PC ? PC->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!PS || PS->Life == EExtractionLife::Alive || PS->Life == EExtractionLife::Downed) return;
	PC->BeginExtractionSpectating();
	if (Character) Character->Destroy();
}

void AExtractionGameMode::LeavePlayer(AController* Player)
{
	if (bEndingPlay || !GetWorld() || GetWorld()->bIsTearingDown) return;
	auto* PS = Player ? Player->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!PS || (PS->Life != EExtractionLife::Alive && PS->Life != EExtractionLife::Downed)) return;
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
			PS && (PS->Life == EExtractionLife::Alive || PS->Life == EExtractionLife::Downed) && GS && GS->Phase ==
			EExtractionPhase::Active)
			SettlePlayer(Player, EExtractionOutcome::Disconnected);
		LeavePlayer(Player);
	}
	Super::Logout(Player);
}

void AExtractionGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	bEndingPlay = true;
	RemoveGuards();
	if (auto* Instance = GetGameInstance())
		if (auto* Sessions = Instance->GetSubsystem<UMultiplayerSessionsSubsystem>())
			Sessions->MultiplayerOnEndSessionComplete.RemoveDynamic(this, &ThisClass::SessionEnded);
	Super::EndPlay(Reason);
}
