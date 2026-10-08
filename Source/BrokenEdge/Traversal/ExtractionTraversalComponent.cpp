#include "ExtractionTraversalComponent.h"
#include "GameFramework/Controller.h"
#include "TraversalMovementComponent.h"
#include "ExtractionLadder.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Net/UnrealNetwork.h"

UExtractionTraversalComponent::UExtractionTraversalComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	AnimationSet = TSoftObjectPtr<UTraversalAnimationSet>(
		FSoftObjectPath(TEXT("/Game/Extraction/Traversal/DA_TraversalAnimations.DA_TraversalAnimations")));
}

void UExtractionTraversalComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const
{
	Super::GetLifetimeReplicatedProps(Out);
	auto& OutLifetimeProps = Out;
	DOREPLIFETIME_CONDITION(UExtractionTraversalComponent, VisualState, COND_SimulatedOnly);
}

void UExtractionTraversalComponent::Publish(const FExtractionTraversalState& State)
{
	if (GetOwner()->GetLocalRole() != ROLE_SimulatedProxy) VisualState = State;
}

bool UExtractionTraversalComponent::Active() const
{
	const auto* C = Cast<ABrokenEdgeCharacter>(GetOwner());
	return C && (C->GetLocalRole() == ROLE_SimulatedProxy
		             ? VisualState.Kind != EExtractionTraversal::None
		             : CastChecked<UTraversalMovementComponent>(C->GetCharacterMovement())->Active());
}

FString UExtractionTraversalComponent::Prompt() const
{
	const auto* C = Cast<ABrokenEdgeCharacter>(GetOwner());
	const auto* M = C ? Cast<UTraversalMovementComponent>(C->GetCharacterMovement()) : nullptr;
	if (!M) return {};
	if (M->OnLadder()) return TEXT("W / S 上下爬梯 · 空格 蹬离 · E 松手");
	if (M->Active()) return TEXT("正在翻越 / 攀上平台");
	return M->FindLadder() ? TEXT("E 爬梯") : FString();
}

void UExtractionTraversalComponent::StopVisuals()
{
	auto* C = Cast<ABrokenEdgeCharacter>(GetOwner());
	if (!C) return;
	if (auto* Anim = C->GetMesh()->GetAnimInstance()) if (BodyMontage) Anim->Montage_Stop(.12f, BodyMontage);
	if (auto* View = C->GetFirstPersonMesh())
		if (auto* Anim = View->GetAnimInstance())
			if (ViewMontage)
				Anim->
					Montage_Stop(.12f, ViewMontage);
	if (IsValid(StowedWeapon))
	{
		StowedWeapon->GetWeaponMesh()->SetVisibility(bWeaponWasVisible);
		StowedWeapon->SetActorHiddenInGame(bWeaponWasHidden);
	}
	StowedWeapon = nullptr;
	ViewMontage = nullptr;
	BodyMontage = nullptr;
}

void UExtractionTraversalComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	StopVisuals();
	Super::EndPlay(Reason);
}

void UExtractionTraversalComponent::TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function)
{
	Super::TickComponent(Delta, Tick, Function);
	auto* C = Cast<ABrokenEdgeCharacter>(GetOwner());
	if (!C) return;
	// Movement can stop ticking after unpossession/death; occupancy must still be released.
	if (C->HasAuthority())
		if (auto* Movement = Cast<UTraversalMovementComponent>(C->GetCharacterMovement()); Movement && Movement->
			Active() && !Movement->CanTraverse())
			Movement->Finish();
	// Authority also needs the body pose for bone-driven hit collision while climbing.
	if (!Active())
	{
		if (bWasActive) StopVisuals();
		bWasActive = false;
		LastSerial = MAX_uint16;
		return;
	}
	const auto& S = VisualState;
	if (!LoadedAnimations) LoadedAnimations = AnimationSet.LoadSynchronous();
	if (!bWasActive || LastSerial != S.Serial)
	{
		StopVisuals();
		// Align the camera only after entry. Changing view yaw inside predicted StartPath
		// changes the entry input sent to authority and can make a top entry fail its facing probe.
		if (S.Kind == EExtractionTraversal::Ladder && C->IsLocallyControlled() && C->Controller)
			C->Controller->SetControlRotation(S.Facing.Rotation());
		StowedWeapon = GetNetMode() != NM_DedicatedServer ? C->GetEquippedWeapon() : nullptr;
		if (StowedWeapon)
		{
			bWeaponWasVisible = StowedWeapon->GetWeaponMesh()->IsVisible();
			bWeaponWasHidden = StowedWeapon->IsHidden();
			StowedWeapon->SetActorHiddenInGame(true);
			StowedWeapon->GetWeaponMesh()->SetVisibility(false);
		}
		RemoteElapsed = S.Elapsed;
		LastRepElapsed = S.Elapsed;
		const FTraversalAnimation* Best = nullptr;
		float Score = BIG_NUMBER;
		const bool Top = IsValid(S.Ladder) && (S.Kind == EExtractionTraversal::LadderExit ? S.End.Z : S.Start.Z) > S.
			Ladder->Point(S.Ladder->Top).Z;
		if (LoadedAnimations)
			for (const auto& Entry : LoadedAnimations->Animations)
			{
				if (Entry.Kind != S.Kind || ((S.Kind == EExtractionTraversal::LadderEnter || S.Kind ==
					EExtractionTraversal::LadderExit) && Entry.bTop != Top))
					continue;
				const float Distance = FMath::Abs(Entry.Height - S.Height);
				if (Distance < Score)
				{
					Score = Distance;
					Best = &Entry;
				}
			}
		if (Best)
		{
			if (auto* Anim = C->GetMesh()->GetAnimInstance(); Anim && Best->ThirdPerson)
				BodyMontage = Anim->PlaySlotAnimationAsDynamicMontage(Best->ThirdPerson,TEXT("DefaultSlot"), .08f, .1f,
				                                                      1.f, 1);
			if (auto* View = C->GetFirstPersonMesh())
				if (auto* Anim = View->GetAnimInstance(); Anim && Best->FirstPerson)
					ViewMontage = Anim->PlaySlotAnimationAsDynamicMontage(
						Best->FirstPerson,TEXT("DefaultSlot"), .08f, .1f, 1.f, 1);
		}
		LastSerial = S.Serial;
		bWasActive = true;
	}
	if (S.Elapsed != LastRepElapsed)
	{
		RemoteElapsed = S.Elapsed;
		LastRepElapsed = S.Elapsed;
	}
	else if (C->GetLocalRole() == ROLE_SimulatedProxy) RemoteElapsed += Delta;
	float Fraction = FMath::Clamp(
		(C->GetLocalRole() == ROLE_SimulatedProxy ? RemoteElapsed : S.Elapsed) / FMath::Max(.01f, S.Duration), 0.f,
		.99f);
	if (S.Kind == EExtractionTraversal::Ladder)
	{
		// Absolute height locks hands/feet to the same rung phase, including pause and descent.
		const float Cycle = IsValid(S.Ladder) ? FMath::Max(20.f, S.Ladder->RungSpacing * 2.f) : 100.f;
		Fraction = FMath::Frac(FMath::Abs(C->GetActorLocation().Z - S.End.Z) / Cycle);
	}
	const auto Position = [Fraction](USkeletalMeshComponent* Mesh, UAnimMontage* Montage)
	{
		if (Mesh && Montage)
			if (auto* Anim = Mesh->GetAnimInstance())
			{
				Anim->Montage_SetPlayRate(Montage, 0.f);
				Anim->Montage_SetPosition(Montage, Fraction * Montage->GetPlayLength());
			}
	};
	Position(C->GetMesh(), BodyMontage);
	Position(C->GetFirstPersonMesh(), ViewMontage);
	if (IsValid(StowedWeapon))
	{
		if (StowedWeapon->GetOwner() == C) StowedWeapon->GetWeaponMesh()->SetVisibility(false);
		else
		{
			StowedWeapon->GetWeaponMesh()->SetVisibility(bWeaponWasVisible);
			StowedWeapon->SetActorHiddenInGame(bWeaponWasHidden);
			StowedWeapon = nullptr;
		}
	}
}
