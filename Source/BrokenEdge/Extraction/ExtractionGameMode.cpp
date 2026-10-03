#include "ExtractionGameMode.h"
#include "ExtractionState.h"
#include "ExtractionPlayerController.h"
#include "ExtractionCombatComponent.h"
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
	if (auto* Sessions = GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>())
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
		}
		else Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Character->ForceNetUpdate();
	}
}

bool AExtractionGameMode::IsCombatActive(AController* Player) const
{
	const auto* GS = GetGameState<AExtractionGameState>();
	const auto* PS = Player ? Player->GetPlayerState<AExtractionPlayerState>() : nullptr;
	return GS && GS->Phase == EExtractionPhase::Active && PS && PS->Life == EExtractionLife::Alive;
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
	auto* GS = GetGameState<AExtractionGameState>();
	if (!GS) return;
	const double Now = GetWorld()->GetTimeSeconds();
	RefreshAliveCount();
	if (GS->Phase == EExtractionPhase::Waiting)
	{
		int32 Ready = 0;
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (It->Get()->HasClientLoadedCurrentWorld() && It->Get()->GetPawn()) ++Ready;
		if (Ready >= ExpectedPlayers)
		{
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
	}
	else if (!bTravelRequested && !bEndRequested && Now >= ReturnAt)
	{
		auto* Sessions = GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>();
		if (Sessions && Sessions->IsBusy()) return;
		bEndRequested = true;
		if (Sessions && Sessions->HasSession()) Sessions->EndSession();
		else SessionEnded(true);
	}
}

void AExtractionGameMode::FinishRound(const FString& Reason)
{
	auto* GS = GetGameState<AExtractionGameState>();
	if (!GS || GS->Phase == EExtractionPhase::Ended) return;
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
	auto* PS = Player ? Player->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!PS || PS->Life == EExtractionLife::Left) return;
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
	LeavePlayer(Player);
	Super::Logout(Player);
}

void AExtractionGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	if (auto* Sessions = GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>())
		Sessions->MultiplayerOnEndSessionComplete.RemoveDynamic(this, &ThisClass::SessionEnded);
	Super::EndPlay(Reason);
}
