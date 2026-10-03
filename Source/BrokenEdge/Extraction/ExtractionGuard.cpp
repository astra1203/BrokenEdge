#include "ExtractionGuard.h"
#include "ExtractionGameMode.h"
#include "ExtractionState.h"
#include "ExtractionAbilities.h"
#include "ExtractionSmoke.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionCombatComponent.h"
#include "ExtractionInteractionComponent.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "NavigationSystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AExtractionPatrolRoute::AExtractionPatrolRoute()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Route")));
}

AExtractionGuard::AExtractionGuard()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = .1f;
	bWantsPlayerState = false;
}

bool AExtractionGuard::Sees(const ABrokenEdgeCharacter* Other) const
{
	const auto* C = Cast<ABrokenEdgeCharacter>(GetPawn());
	const auto* PS = Other ? Other->GetPlayerState<AExtractionPlayerState>() : nullptr;
	if (!C || !PS || Other->IsDefeated() || (PS->Life != EExtractionLife::Alive && PS->Life != EExtractionLife::Downed)) return false;
	const FVector Delta = Other->GetActorLocation() - C->GetActorLocation();
	if (Delta.SizeSquared() > FMath::Square(3000.f) || FVector::DotProduct(C->GetActorForwardVector(), Delta.GetSafeNormal()) < .5f) return false;
	if (AExtractionSmoke::Blocks(GetWorld(), C->GetPawnViewLocation(), Other->GetPawnViewLocation())) return false;
	return LineOfSightTo(Other);
}

void AExtractionGuard::Tick(float Delta)
{
	Super::Tick(Delta);
	auto* C = Cast<ABrokenEdgeCharacter>(GetPawn());
	const auto* GS = GetWorld()->GetGameState<AExtractionGameState>();
	if (!C || C->IsDefeated()) { StopMovement(); ClearFocus(EAIFocusPriority::Gameplay); Target = nullptr; State = EExtractionGuardState::Dead; return; }
	const auto* GM = GetWorld()->GetAuthGameMode<AExtractionGameMode>();
	if (!GS || GS->Phase != EExtractionPhase::Active || !GM || !GM->Store() || !GM->Store()->IsAvailable()) { StopMovement(); return; }
	const double Now = GetWorld()->GetTimeSeconds();
	ABrokenEdgeCharacter* Seen = nullptr;
	float Best = TNumericLimits<float>::Max();
	for (APlayerState* PS : GS->PlayerArray)
		if (auto* Other = Cast<ABrokenEdgeCharacter>(PS->GetPawn()); Sees(Other))
		{
			++VisibleChecks;
			const float Score = FVector::DistSquared(C->GetActorLocation(), Other->GetActorLocation())
				+ (Other->GetExtractionAbilities()->IsDowned() ? 10000000.f : 0.f);
			if (Score < Best) { Seen = Other; Best = Score; }
		}
	if (Seen)
	{
		if (Target != Seen) ReactAt = Now + .7;
		Target = Seen;
		LastKnown = Seen->GetActorLocation();
		LostAt = Now;
		State = EExtractionGuardState::Engage;
		SetFocus(Seen);
		StopMovement();
		if (auto* W = C->GetEquippedWeapon(); W && Now >= ReactAt)
		{
			if (W->IsEmpty()) C->GetExtractionCombat()->BeginServerReload();
			else
			{
				const uint32 Before = C->GetExtractionCombat()->GetAcceptedShots();
				C->GetExtractionCombat()->FireGuard(LastKnown + FVector(0, 0, 10) + FMath::VRand() * 20);
				Shots += C->GetExtractionCombat()->GetAcceptedShots() - Before;
			}
		}
		return;
	}
	if (Target)
	{
		Target = nullptr;
		ClearFocus(EAIFocusPriority::Gameplay);
		State = EExtractionGuardState::Search;
		NextMove = 0;
	}
	if (State == EExtractionGuardState::Search)
	{
		if (Now - LostAt < 5)
		{
			if (Now >= NextMove) { MoveToLocation(LastKnown, 80); NextMove = Now + 1; }
			return;
		}
		State = EExtractionGuardState::Return;
		NextMove = 0;
	}
	if (!Route || Route->Points.IsEmpty()) return;
	const FVector Point = Route->Points[PatrolIndex % Route->Points.Num()];
	if (FVector::DistSquared2D(C->GetActorLocation(), Point) < FMath::Square(110.f))
	{
		++PatrolArrivals;
		PatrolIndex = (PatrolIndex + 1) % Route->Points.Num();
		State = EExtractionGuardState::Patrol;
		NextMove = Now + .6;
	}
	if (Now >= NextMove) { MoveToLocation(Point, 70); NextMove = Now + 1; }
}

void AExtractionGameMode::SpawnGuards()
{
	int32 Count = FMath::Clamp(GetDefault<UExtractionRules>()->Guards, 0, 4);
	// Explicit server scenario configuration; clients cannot change this through a URL or RPC.
	FParse::Value(FCommandLine::Get(), TEXT("ExtractionGuards="), Count);
	Count = FMath::Clamp(Count, 0, 4);
	TArray<AExtractionPatrolRoute*> Routes;
	for (TActorIterator<AExtractionPatrolRoute> It(GetWorld()); It; ++It) if (!It->Points.IsEmpty()) Routes.Add(*It);
	Routes.Sort([](const auto& A, const auto& B) { return A.GetName() < B.GetName(); });
	if (Count && Routes.IsEmpty()) UE_LOG(LogTemp, Error, TEXT("EXTRACTION_GUARDS missing cooked patrol routes"));
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	for (int32 I = 0; I < Count && !Routes.IsEmpty(); ++I)
	{
		auto* Route = Routes[I % Routes.Num()];
		const int32 Index = (I / Routes.Num()) % Route->Points.Num();
		FNavLocation Projected;
		if (!Nav || !Nav->ProjectPointToNavigation(Route->Points[Index], Projected, FVector(200, 200, 300)))
		{
			UE_LOG(LogTemp, Error, TEXT("EXTRACTION_GUARDS route lacks navigation"));
			continue;
		}
		const FTransform Transform(FRotator::ZeroRotator, Projected.Location + FVector(0, 0, 100));
		auto* C = GetWorld()->SpawnActorDeferred<ABrokenEdgeCharacter>(DefaultPawnClass, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!C) continue;
		C->bExtractionGuard = true;
		C->ExtractionGuardId = I;
		C->FinishSpawning(Transform);
		C->SetHealth(100);
		C->SetShield(0);
		C->GetCharacterMovement()->MaxWalkSpeed = 220;

		C->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		auto* AI = GetWorld()->SpawnActor<AExtractionGuard>();
		AI->Route = Route;
		AI->PatrolIndex = Index;
		AI->Possess(C);
		C->GuardInventory->InitializeGuard();
		C->ForceNetUpdate();
		UE_LOG(LogTemp, Display, TEXT("EXTRACTION_GUARD_SPAWN Actor=%s Route=%s"), *C->GetName(), *Route->GetName());
	}
}

void AExtractionGameMode::RemoveGuards()
{
	for (TActorIterator<AExtractionGuard> It(GetWorld()); It; ++It)
	{
		if (auto* C = Cast<ABrokenEdgeCharacter>(It->GetPawn())) { C->GuardInventory->ClearSettled(); C->Destroy(); }
		It->Destroy();
	}
	for (TActorIterator<AExtractionSmoke> It(GetWorld()); It; ++It) It->Destroy();
}
