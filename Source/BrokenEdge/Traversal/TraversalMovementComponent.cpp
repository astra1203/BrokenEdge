#include "TraversalMovementComponent.h"
#include "ExtractionLadder.h"
#include "ExtractionTraversalComponent.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Extraction/ExtractionCombatComponent.h"
#include "BrokenEdge/Extraction/ExtractionInteractionComponent.h"
#include "BrokenEdge/Extraction/ExtractionState.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "EngineUtils.h"
#include "Engine/PackageMapClient.h"

namespace
{
	constexpr uint8 TraversalMode = 1;

	class FTraversalSavedMove : public FSavedMove_Character
	{
	public:
		bool Interact = false, Detach = false, JumpDetach = false, Traversing = false, Obstacle = false;

		virtual void Clear() override
		{
			Super::Clear();
			Interact = Detach = JumpDetach = Traversing = Obstacle = false;
		}

		using Super = FSavedMove_Character;

		virtual uint8 GetCompressedFlags() const override
		{
			return Super::GetCompressedFlags() | (Interact ? FLAG_Custom_0 : 0)
				| (Detach ? FLAG_Custom_1 : 0) | (JumpDetach ? FLAG_Custom_2 : 0) | (Obstacle ? FLAG_Custom_3 : 0);
		}

		virtual void SetMoveFor(ACharacter* C, float Dt, FVector const& Accel,
		                        FNetworkPredictionData_Client_Character& Data) override
		{
			Super::SetMoveFor(C, Dt, Accel, Data);
			const auto* M = CastChecked<UTraversalMovementComponent>(C->GetCharacterMovement());
			Interact = M->bInteractRequested;
			Detach = M->bDetachRequested;
			JumpDetach = M->bJumpDetachRequested;
			Traversing = M->Active();
			Obstacle = M->bObstacleRequested;
		}

		virtual bool CanCombineWith(const FSavedMovePtr& Other, ACharacter* C, float MaxDelta) const override
		{
			const auto* M = static_cast<const FTraversalSavedMove*>(Other.Get());
			// Preserve one-shot edges and curved sweeps exactly during prediction/replay.
			return !Traversing && !M->Traversing && !Interact && !Detach && !JumpDetach
				&& !Obstacle && !M->Obstacle && !M->Interact && !M->Detach && !M->JumpDetach && Super::CanCombineWith(
					Other, C, MaxDelta);
		}

		virtual void PrepMoveFor(ACharacter* C) override
		{
			Super::PrepMoveFor(C);
			auto* M = CastChecked<UTraversalMovementComponent>(C->GetCharacterMovement());
			M->bInteractRequested = Interact;
			M->bDetachRequested = Detach;
			M->bJumpDetachRequested = JumpDetach;
			M->bObstacleRequested = Obstacle;
		}
	};

	class FTraversalPrediction : public FNetworkPredictionData_Client_Character
	{
	public:
		explicit FTraversalPrediction(const UCharacterMovementComponent& M) : FNetworkPredictionData_Client_Character(M)
		{
		}

		virtual FSavedMovePtr AllocateNewMove() override { return MakeShared<FTraversalSavedMove>(); }
	};
}

void FTraversalMoveResponse::ServerFillResponseData(const UCharacterMovementComponent& Movement,
                                                    const FClientAdjustment& Adjustment)
{
	FCharacterMoveResponseDataContainer::ServerFillResponseData(Movement, Adjustment);
	State = CastChecked<UTraversalMovementComponent>(&Movement)->State;
}

bool FTraversalMoveResponse::Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map)
{
	const bool Result = FCharacterMoveResponseDataContainer::Serialize(Movement, Ar, Map);
	if (IsCorrection())
	{
		Ar << State.Kind << State.Start << State.Raised << State.Across << State.End << State.Facing;
		Ar << State.Contact;
		Ar << State.Elapsed << State.Duration << State.Height << State.Serial;
		UObject* Ladder = State.Ladder.Get();
		UObject* Support = State.Support.Get();
		Map->SerializeObject(Ar, AExtractionLadder::StaticClass(), Ladder);
		Map->SerializeObject(Ar, UPrimitiveComponent::StaticClass(), Support);
		if (Ar.IsLoading())
		{
			State.Ladder = Cast<AExtractionLadder>(Ladder);
			State.Support = Cast<UPrimitiveComponent>(Support);
		}
	}
	return Result && !Ar.IsError();
}

UTraversalMovementComponent::UTraversalMovementComponent()
{
	SetMoveResponseDataContainer(ResponseData);
}

FNetworkPredictionData_Client* UTraversalMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
		const_cast<UTraversalMovementComponent*>(this)->ClientPredictionData = new
			FTraversalPrediction(*this);
	return ClientPredictionData;
}

void UTraversalMovementComponent::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& Response)
{
	// Ignore stale corrections exactly as the base CMC does. Never overwrite predicted state on a good ACK.
	const auto* Prediction = GetPredictionData_Client_Character();
	if (Response.IsCorrection() && Prediction && Prediction->GetSavedMoveIndex(Response.ClientAdjustment.TimeStamp) !=
		INDEX_NONE)
		State = static_cast<const FTraversalMoveResponse&>(Response).State;
	Super::ClientHandleMoveResponse(Response);
	Publish();
}

bool UTraversalMovementComponent::Enabled() const
{
	const auto* C = Cast<ABrokenEdgeCharacter>(CharacterOwner);
	return C && !C->bExtractionGuard && GetWorld()->GetGameState<AExtractionGameState>();
}

bool UTraversalMovementComponent::CanTraverse() const
{
	const auto* C = Cast<ABrokenEdgeCharacter>(CharacterOwner);
	const auto* PS = C ? C->GetPlayerState<AExtractionPlayerState>() : nullptr;
	const auto* GS = GetWorld()->GetGameState<AExtractionGameState>();
	return Enabled() && PS && PS->Life == EExtractionLife::Alive && GS->Phase == EExtractionPhase::Active
		&& C->Controller && !C->IsDefeated() && !C->bDisableGameplay && !C->bIsCrouched;
}

bool UTraversalMovementComponent::ClearAt(const FVector& Position) const
{
	const auto* Capsule = CharacterOwner->GetCapsuleComponent();
	return !GetWorld()->OverlapBlockingTestByChannel(Position, FQuat::Identity, ECC_Pawn,
	                                                 FCollisionShape::MakeCapsule(
		                                                 Capsule->GetScaledCapsuleRadius(),
		                                                 Capsule->GetScaledCapsuleHalfHeight()),
	                                                 FCollisionQueryParams(
		                                                 SCENE_QUERY_STAT(TraversalClearance), false, CharacterOwner));
}

bool UTraversalMovementComponent::ClearSegment(const FVector& From, const FVector& To) const
{
	const auto* Capsule = CharacterOwner->GetCapsuleComponent();
	FHitResult Hit;
	return !GetWorld()->SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_Pawn,
	                                         FCollisionShape::MakeCapsule(
		                                         Capsule->GetScaledCapsuleRadius(),
		                                         Capsule->GetScaledCapsuleHalfHeight()),
	                                         FCollisionQueryParams(
		                                         SCENE_QUERY_STAT(TraversalPath), false, CharacterOwner));
}

bool UTraversalMovementComponent::Standable(const FVector& Feet, FVector& Center) const
{
	const float Radius = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius();
	FHitResult Hit;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(TraversalFloor), false, CharacterOwner);
	// A centre ray alone accepts ledges too narrow to stand on. Require the capsule footprint.
	for (const FVector2D Offset : {
		     FVector2D(0, 0), FVector2D(Radius * .8f, 0), FVector2D(-Radius * .8f, 0),
		     FVector2D(0, Radius * .8f), FVector2D(0, -Radius * .8f)
	     })
	{
		const FVector Point = Feet + FVector(Offset.X, Offset.Y, 0);
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Point + FVector(0, 0, 12), Point - FVector(0, 0, 12), ECC_Pawn,
		                                          Params)
			|| !IsWalkable(Hit) || FMath::Abs(Hit.ImpactPoint.Z - Feet.Z) > 8.f)
			return false;
	}
	Center = Feet + FVector(0, 0, CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 3.f);
	return ClearAt(Center);
}

bool UTraversalMovementComponent::TryObstacle()
{
	if (!CanTraverse() || Active() || !IsMovingOnGround()) return false;
	const auto* Rules = GetDefault<UExtractionTraversalRules>();
	const float Half = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float Radius = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius();
	FVector Feet = UpdatedComponent->GetComponentLocation() - FVector(0, 0, Half);
	// Walking deliberately floats the capsule above the floor. Measure obstacle height
	// from the supporting surface, otherwise an over-limit ledge can pass the probe.
	if (CurrentFloor.IsWalkableFloor()) Feet.Z -= CurrentFloor.GetDistanceToFloor();
	const FVector Facing = FRotator(0, CharacterOwner->GetControlRotation().Yaw, 0).Vector();
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(TraversalProbe), false, CharacterOwner);
	FHitResult Front, Top;
	const FVector Probe = Feet + FVector(0, 0, MaxStepHeight + 6.f);
	if (!GetWorld()->LineTraceSingleByChannel(Front, Probe, Probe + Facing * (Radius + Rules->Reach), ECC_Pawn, Params))
		return false;
	if (!Front.Component.IsValid() || Front.Component->Mobility != EComponentMobility::Static ||
		FVector::DotProduct(Front.ImpactNormal, -Facing) < .5f)
		return false;
	const FVector Lip = Front.ImpactPoint + Facing * 5.f;
	if (!GetWorld()->LineTraceSingleByChannel(Top, FVector(Lip.X, Lip.Y, Feet.Z + Rules->MantleHeight + 12.f),
	                                          FVector(Lip.X, Lip.Y, Feet.Z + MaxStepHeight), ECC_Pawn,
	                                          Params) || !IsWalkable(Top))
		return false;
	const float Height = Top.ImpactPoint.Z - Feet.Z;
	if (Height <= MaxStepHeight || Height > Rules->MantleHeight + .1f)
	{
		LastRejection = TEXT("height");
		return false;
	}
	FExtractionTraversalState Candidate;
	Candidate.Start = UpdatedComponent->GetComponentLocation();
	Candidate.Facing = Facing;
	Candidate.Height = Height;
	Candidate.Support = Top.GetComponent();
	Candidate.Contact = Top.ImpactPoint;
	Candidate.Raised = FVector(Candidate.Start.X, Candidate.Start.Y, Top.ImpactPoint.Z + Half + 5.f);
	Candidate.Kind = EExtractionTraversal::Mantle;
	Candidate.Duration = Rules->MantleSeconds;
	bool Found = false;
	if (Height <= Rules->VaultHeight + .1f)
	{
		// Find the far edge, then require a clear landing at approximately the departure floor height.
		for (float Depth = 10.f; Depth <= Rules->VaultDepth; Depth += 5.f)
		{
			FHitResult Edge, Landing;
			FVector Point = Front.ImpactPoint + Facing * Depth;
			Point.Z = Top.ImpactPoint.Z;
			if (GetWorld()->LineTraceSingleByChannel(Edge, Point + FVector(0, 0, 8), Point - FVector(0, 0, 12),
			                                         ECC_Pawn, Params))
				continue;
			Point += Facing * (Radius + 6.f);
			if (GetWorld()->LineTraceSingleByChannel(Landing, Point, FVector(Point.X, Point.Y, Feet.Z - 30), ECC_Pawn,
			                                         Params)
				&& IsWalkable(Landing) && FMath::Abs(Landing.ImpactPoint.Z - Feet.Z) <= MaxStepHeight
				&& Standable(Landing.ImpactPoint, Candidate.End))
			{
				Candidate.Kind = EExtractionTraversal::Vault;
				Candidate.Duration = Rules->VaultSeconds;
				Found = true;
			}
			break;
		}
	}
	if (!Found)
	{
		FVector Point = Top.ImpactPoint + Facing * (Radius + 8.f);
		if (!Standable(Point, Candidate.End))
		{
			LastRejection = TEXT("landing");
			return false;
		}
	}
	Candidate.Across = FVector(Candidate.End.X, Candidate.End.Y, Candidate.Raised.Z);
	return StartPath(Candidate);
}

bool UTraversalMovementComponent::DoJump(bool bReplayingMoves, float DeltaTime)
{
	if (Enabled())
	{
		if (Active()) return false;
		if (TryObstacle())
		{
			// CheckJumpInput runs before SetMoveFor. Entering MOVE_Custom resets bPressedJump,
			// so preserve this edge independently for server probing and correction replay.
			bObstacleRequested = true;
			return true;
		}
	}
	return Super::DoJump(bReplayingMoves, DeltaTime);
}

AExtractionLadder* UTraversalMovementComponent::FindLadder() const
{
	if (!CanTraverse() || Active()) return nullptr;
	const FVector Here = UpdatedComponent->GetComponentLocation();
	const float Half = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Up(0, 0, Half + 3);
	float Best = FMath::Square(GetDefault<UExtractionTraversalRules>()->LadderReach);
	AExtractionLadder* Result = nullptr;
	const FVector Look = FRotator(0, CharacterOwner->GetControlRotation().Yaw, 0).Vector();
	for (TActorIterator<AExtractionLadder> It(GetWorld()); It; ++It)
	{
		if (It->GetActorForwardVector().GetSafeNormal2D().IsNearlyZero()) continue;
		if (IsValid(It->Occupant) && It->Occupant != CharacterOwner) continue;
		const FVector Entry = FVector::DistSquared(Here, It->Point(It->BottomExit) + Up) < FVector::DistSquared(
			                      Here, It->Point(It->TopExit) + Up)
			                      ? It->Point(It->BottomExit) + Up
			                      : It->Point(It->TopExit) + Up;
		const float Distance = FVector::DistSquared(Here, Entry);
		if (Distance < Best && FVector::DotProduct(Look, (Entry - Here).GetSafeNormal2D()) > -.25f &&
			ClearSegment(Here, Entry))
		{
			Best = Distance;
			Result = *It;
		}
	}
	return Result;
}

bool UTraversalMovementComponent::RequestInteract()
{
	if (!Enabled()) return false;
	if (OnLadder())
	{
		RequestDetach(false);
		return true;
	}
	if (Active()) return true;
	if (!FindLadder()) return false;
	bInteractRequested = true;
	return true;
}

void UTraversalMovementComponent::RequestDetach(bool bJump)
{
	bDetachRequested = true;
	bJumpDetachRequested = bJump;
}

bool UTraversalMovementComponent::TryLadder()
{
	auto* Ladder = FindLadder();
	if (!Ladder)
	{
		LastRejection = TEXT("ladder_unavailable");
		return false;
	}
	const float Half = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Up(0, 0, Half + 3);
	const FVector Here = UpdatedComponent->GetComponentLocation();
	const bool Top = FVector::DistSquared(Here, Ladder->Point(Ladder->TopExit) + Up) < FVector::DistSquared(
		Here, Ladder->Point(Ladder->BottomExit) + Up);
	FExtractionTraversalState Candidate;
	Candidate.Kind = EExtractionTraversal::LadderEnter;
	Candidate.Ladder = Ladder;
	Candidate.Start = Here;
	Candidate.End = Ladder->Point(Top ? Ladder->Top : Ladder->Bottom) + Up;
	if (Top)
		Candidate.End.Z -= FMath::Min(GetDefault<UExtractionTraversalRules>()->LadderRoofReach,
		                              Ladder->Point(Ladder->Top).Z - Ladder->Point(Ladder->Bottom).Z - 30.f);
	Candidate.Raised = FVector(Here.X, Here.Y, FMath::Max(Here.Z, Candidate.End.Z) + 3);
	Candidate.Across = FVector(Candidate.End.X, Candidate.End.Y, Candidate.Raised.Z);
	Candidate.Facing = Ladder->GetActorForwardVector().GetSafeNormal2D();
	Candidate.Duration = .35f;
	if (CharacterOwner->HasAuthority() && !Ladder->Claim(CharacterOwner)) return false;
	if (!StartPath(Candidate))
	{
		Ladder->Release(CharacterOwner);
		return false;
	}
	return true;
}

void UTraversalMovementComponent::CancelActions()
{
	if (auto* C = Cast<ABrokenEdgeCharacter>(CharacterOwner))
	{
		C->GetExtractionInteraction()->StopCombatInput();
		C->GetExtractionCombat()->CancelReload();
		C->GetExtractionCombat()->CancelSwap();
		if (C->HasAuthority())
		{
			C->GetExtractionInteraction()->CancelMedical();
			C->GetExtractionInteraction()->Close();
		}
		if (auto* Anim = C->GetMesh()->GetAnimInstance()) Anim->StopAllMontages(.1f);
		if (auto* View = C->GetFirstPersonMesh()) if (auto* Anim = View->GetAnimInstance()) Anim->StopAllMontages(.1f);
	}
}

bool UTraversalMovementComponent::StartPath(FExtractionTraversalState Candidate)
{
	if (!ClearAt(Candidate.End) || !ClearSegment(Candidate.Start, Candidate.Raised)
		|| !ClearSegment(Candidate.Raised, Candidate.Across) || !ClearSegment(Candidate.Across, Candidate.End))
	{
		LastRejection = TEXT("blocked_path");
		return false;
	}
	Candidate.Serial = State.Serial + 1;
	State = Candidate;
	LastRejection.Reset();
	Velocity = FVector::ZeroVector;
	CancelActions();
	SetMovementMode(MOVE_Custom, TraversalMode);
	Publish();
	return true;
}

void UTraversalMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bInteractRequested = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	bDetachRequested = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
	bJumpDetachRequested = (Flags & FSavedMove_Character::FLAG_Custom_2) != 0;
	bObstacleRequested = (Flags & FSavedMove_Character::FLAG_Custom_3) != 0;
}

void UTraversalMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	if (Active() && (!CanTraverse() || (OnLadder() && !IsValid(State.Ladder)))) Finish();
	if (bDetachRequested && OnLadder()) Finish(bJumpDetachRequested);
	else if (bInteractRequested && !Active()) TryLadder();
	else if (bObstacleRequested && !Active()) TryObstacle();
	bInteractRequested = bDetachRequested = bJumpDetachRequested = false;
	bObstacleRequested = false;
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
}

FVector UTraversalMovementComponent::PathPosition(float Fraction) const
{
	if (Fraction < .35f) return FMath::Lerp(State.Start, State.Raised, Fraction / .35f);
	if (Fraction < .75f) return FMath::Lerp(State.Raised, State.Across, (Fraction - .35f) / .4f);
	return FMath::Lerp(State.Across, State.End, (Fraction - .75f) / .25f);
}

void UTraversalMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
	if (CustomMovementMode != TraversalMode)
	{
		Super::PhysCustom(DeltaTime, Iterations);
		return;
	}
	if (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) return;
	if (!Active() || !CanTraverse())
	{
		Finish();
		StartNewPhysics(DeltaTime, Iterations);
		return;
	}
	float Remaining = DeltaTime;
	while (Remaining >= MIN_TICK_TIME && Active())
	{
		const float Step = FMath::Min(Remaining, 1.f / 120.f);
		Remaining -= Step;
		const FVector Before = UpdatedComponent->GetComponentLocation();
		FVector Desired;
		if (State.Kind == EExtractionTraversal::Ladder)
		{
			if (!IsValid(State.Ladder))
			{
				Finish();
				break;
			}
			const float Input = FMath::Clamp(FVector::DotProduct(Acceleration, State.Facing) / GetMaxAcceleration(),
			                                 -1.f, 1.f);
			const float Half = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 3;
			const FVector Bottom = State.Ladder->Point(State.Ladder->Bottom) + FVector(0, 0, Half);
			FVector Top = State.Ladder->Point(State.Ladder->Top) + FVector(0, 0, Half);
			Top.Z -= FMath::Min(GetDefault<UExtractionTraversalRules>()->LadderRoofReach, Top.Z - Bottom.Z - 30.f);
			Desired = Before + FVector(0, 0, Input * GetDefault<UExtractionTraversalRules>()->LadderSpeed * Step);
			Desired.Z = FMath::Clamp(Desired.Z, Bottom.Z, Top.Z);
			if ((Input > .1f && Desired.Z >= Top.Z - .1f) || (Input < -.1f && Desired.Z <= Bottom.Z + .1f))
			{
				FVector End;
				if (Standable(State.Ladder->Point(Input > 0 ? State.Ladder->TopExit : State.Ladder->BottomExit), End))
				{
					FExtractionTraversalState Exit = State;
					Exit.Kind = EExtractionTraversal::LadderExit;
					Exit.Start = Before;
					Exit.End = End;
					Exit.Raised = FVector(Before.X, Before.Y, FMath::Max(Before.Z, End.Z) + 5);
					Exit.Across = FVector(End.X, End.Y, Exit.Raised.Z);
					Exit.Elapsed = 0;
					Exit.Duration = Input > 0 ? 1.f : .45f;
					if (StartPath(Exit)) continue;
				}
			}
			State.Elapsed += FMath::Abs(Desired.Z - Before.Z) / GetDefault<UExtractionTraversalRules>()->LadderSpeed;
		}
		else
		{
			if ((State.Kind == EExtractionTraversal::Vault || State.Kind == EExtractionTraversal::Mantle) && !IsValid(
				State.Support))
			{
				Finish();
				break;
			}
			State.Elapsed = FMath::Min(State.Duration, State.Elapsed + Step);
			Desired = PathPosition(State.Elapsed / State.Duration);
		}
		FHitResult Hit;
		SafeMoveUpdatedComponent(Desired - Before, FRotator(0, State.Facing.Rotation().Yaw, 0).Quaternion(), true, Hit);
		Velocity = (UpdatedComponent->GetComponentLocation() - Before) / Step;
		if (Hit.IsValidBlockingHit() || Hit.bStartPenetrating)
		{
			LastRejection = TEXT("path_changed");
			Finish();
			break;
		}
		if (State.Kind != EExtractionTraversal::Ladder && State.Elapsed >= State.Duration - KINDA_SMALL_NUMBER)
		{
			if (State.Kind == EExtractionTraversal::LadderEnter)
			{
				State.Kind = EExtractionTraversal::Ladder;
				State.Elapsed = 0;
				++State.Serial;
				Velocity = FVector::ZeroVector;
			}
			else Finish();
		}
	}
	Publish();
}

void UTraversalMovementComponent::Finish(bool bJumpAway)
{
	const FVector Facing = State.Facing;
	if (IsValid(State.Ladder)) State.Ladder->Release(CharacterOwner);
	const uint16 Serial = State.Serial + 1;
	State = FExtractionTraversalState();
	State.Serial = Serial;
	SetMovementMode(MOVE_Falling);
	Velocity = bJumpAway ? -Facing * 220.f + FVector(0, 0, 220) : FVector::ZeroVector;
	FFindFloorResult Floor;
	FindFloor(UpdatedComponent->GetComponentLocation(), Floor, false);
	if (!bJumpAway && Floor.IsWalkableFloor() && Floor.FloorDist <= MAX_FLOOR_DIST) SetMovementMode(MOVE_Walking);
	Publish();
}

void UTraversalMovementComponent::Publish()
{
	if (auto* C = Cast<ABrokenEdgeCharacter>(CharacterOwner)) C->GetTraversal()->Publish(State);
}

void UTraversalMovementComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (IsValid(State.Ladder)) State.Ladder->Release(CharacterOwner);
	Super::EndPlay(Reason);
}
