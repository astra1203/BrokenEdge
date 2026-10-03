#include "ExtractionAbilities.h"
#include "ExtractionState.h"
#include "ExtractionCombatComponent.h"
#include "ExtractionInteractionComponent.h"
#include "ExtractionSmoke.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"

UExtractionAbilities::UExtractionAbilities()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = .05f;
}

ABrokenEdgeCharacter* UExtractionAbilities::Character() const { return Cast<ABrokenEdgeCharacter>(GetOwner()); }

void UExtractionAbilities::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UExtractionAbilities, Action);
	DOREPLIFETIME(UExtractionAbilities, ActionEnd);
	DOREPLIFETIME(UExtractionAbilities, Target);
	DOREPLIFETIME(UExtractionAbilities, Rescuer);
	DOREPLIFETIME_CONDITION(UExtractionAbilities, HealReadyAt, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UExtractionAbilities, SmokeReadyAt, COND_OwnerOnly);
}

bool UExtractionAbilities::Friendly(const ABrokenEdgeCharacter* A, const ABrokenEdgeCharacter* B)
{
	const auto* AP = A ? A->GetPlayerState<AExtractionPlayerState>() : nullptr;
	const auto* BP = B ? B->GetPlayerState<AExtractionPlayerState>() : nullptr;
	return AP && BP && AP->ExtractionSquad > 0 && AP->ExtractionSquad == BP->ExtractionSquad;
}

bool UExtractionAbilities::Reach(const ABrokenEdgeCharacter* A, const ABrokenEdgeCharacter* B, float Range)
{
	if (!IsValid(A) || !IsValid(B) || FVector::DistSquared(A->GetActorLocation(), B->GetActorLocation()) >
		FMath::Square(Range)) return false;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(ExtractionAbility), false, A);
	Q.AddIgnoredActor(B);
	for (TActorIterator<AWeapon> It(A->GetWorld()); It; ++It)
		if (It->GetOwner() == A || It->GetOwner() == B) Q.AddIgnoredActor(*It);
	FHitResult Hit;
	return !A->GetWorld()->LineTraceSingleByChannel(Hit, A->GetPawnViewLocation(), B->GetActorLocation(),
	                                                ECC_Visibility, Q);
}

bool UExtractionAbilities::IsDowned() const
{
	const auto* PS = Character() ? Character()->GetPlayerState<AExtractionPlayerState>() : nullptr;
	return PS && PS->Life == EExtractionLife::Downed;
}

bool UExtractionAbilities::CanStart() const
{
	const auto* C = Character();
	return C && !C->bExtractionGuard && C->GetExtractionCombat()->CanAct()
		&& C->GetCombatState() == ECombatState::ECS_Unoccuiped && !C->GetExtractionInteraction()->InventoryOpen;
}

ABrokenEdgeCharacter* UExtractionAbilities::RescueFocus() const
{
	const auto* C = Character();
	if (!C || !CanStart()) return nullptr;
	ABrokenEdgeCharacter* Best = nullptr;
	double Distance = TNumericLimits<double>::Max();
	for (TActorIterator<ABrokenEdgeCharacter> It(GetWorld()); It; ++It)
		if (*It != C && Friendly(C, *It) && It->GetExtractionAbilities()->IsDowned()
			&& Reach(C, *It, GetDefault<UExtractionRules>()->ReviveRange))
		{
			const auto Delta = It->GetActorLocation() - C->GetActorLocation();
			if (FVector::DotProduct(C->GetControlRotation().Vector().GetSafeNormal2D(), Delta.GetSafeNormal2D()) < .4f)
				continue;
			if (Delta.SizeSquared() < Distance)
			{
				Best = *It;
				Distance = Delta.SizeSquared();
			}
		}
	return Best;
}

ABrokenEdgeCharacter* UExtractionAbilities::HealFocus() const
{
	auto* C = Character();
	if (!C) return nullptr;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(ExtractionHealAim), false, C);
	if (C->GetEquippedWeapon()) Q.AddIgnoredActor(C->GetEquippedWeapon());
	FHitResult Hit;
	const auto Start = C->GetPawnViewLocation();
	GetWorld()->LineTraceSingleByChannel(
		Hit, Start, Start + C->GetControlRotation().Vector() * GetDefault<UExtractionRules>()->HealRange,
		ECC_Visibility, Q);
	if (auto* Other = Cast<ABrokenEdgeCharacter>(Hit.GetActor())) return Other;
	return C;
}

void UExtractionAbilities::Request(EExtractionAction Kind, ABrokenEdgeCharacter* Other)
{
	ServerBegin(++LocalSequence, Kind, Other);
}

void UExtractionAbilities::ReleaseRescue() { ServerCancel(++LocalSequence); }

void UExtractionAbilities::ServerCancel_Implementation(uint32 Sequence)
{
	if (Sequence <= LastSequence) return;
	LastSequence = Sequence;
	Cancel();
}

void UExtractionAbilities::ServerBegin_Implementation(uint32 Sequence, EExtractionAction Kind,
                                                      ABrokenEdgeCharacter* Other)
{
	if (Sequence <= LastSequence) return;
	LastSequence = Sequence;
	if (!CanStart() || Action != EExtractionAction::None) return;
	const auto* R = GetDefault<UExtractionRules>();
	const double Now = GetWorld()->GetTimeSeconds();
	if (Kind != EExtractionAction::Revive && Kind != EExtractionAction::Heal && Kind != EExtractionAction::Smoke)
		return;
	if ((Kind == EExtractionAction::Heal && Now < HealReadyAt) || (Kind == EExtractionAction::Smoke && Now <
		SmokeReadyAt)) return;
	auto* C = Character();
	C->GetExtractionInteraction()->CancelMedical();
	Action = Kind;
	Target = Other;
	if (!ValidTarget())
	{
		Cancel();
		return;
	}
	if (Kind == EExtractionAction::Revive)
	{
		auto* Ability = Target->GetExtractionAbilities();
		if (IsValid(Ability->Rescuer) && Ability->Rescuer != C)
		{
			Cancel();
			return;
		}
		Ability->Rescuer = C;
		Target->ForceNetUpdate();
	}
	if (Kind == EExtractionAction::Smoke)
	{
		FCollisionQueryParams Q(SCENE_QUERY_STAT(ExtractionSmokeAim), false, C);
		if (C->GetEquippedWeapon()) Q.AddIgnoredActor(C->GetEquippedWeapon());
		const auto Eye = C->GetPawnViewLocation();
		SmokePoint = Eye + C->GetControlRotation().Vector() * R->SmokeRange;
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, SmokePoint, ECC_Visibility, Q)) SmokePoint = Hit.ImpactPoint
			+ Hit.ImpactNormal * 10;
	}
	StartedAt = C->GetActorLocation();
	ActionEnd = Now + (Kind == EExtractionAction::Revive
		                   ? R->ReviveSeconds
		                   : Kind == EExtractionAction::Heal
		                   ? R->HealSeconds
		                   : R->SmokeCastSeconds);
	C->GetCombat()->FireButtonPressed(false);
	C->GetExtractionInteraction()->StopCombatInput();
	C->ForceNetUpdate();
}

bool UExtractionAbilities::ValidTarget() const
{
	if (Action == EExtractionAction::Smoke) return true;
	const auto* C = Character();
	const auto* R = GetDefault<UExtractionRules>();
	if (!IsValid(Target) || !Friendly(C, Target)) return false;
	const auto* PS = Target->GetPlayerState<AExtractionPlayerState>();
	if (Action == EExtractionAction::Revive)
		return Target != C && PS->Life == EExtractionLife::Downed && Reach(C, Target, R->ReviveRange);
	return Action == EExtractionAction::Heal && PS->Life == EExtractionLife::Alive
		&& Target->GetHealth() < Target->GetMaxHealth() && (Target == C || Reach(C, Target, R->HealRange));
}

void UExtractionAbilities::Cancel()
{
	if (!GetOwner()->HasAuthority()) return;
	if (IsValid(Target) && Target->GetExtractionAbilities()->Rescuer == Character())
	{
		Target->GetExtractionAbilities()->Rescuer = nullptr;
		Target->ForceNetUpdate();
	}
	Action = EExtractionAction::None;
	ActionEnd = 0;
	Target = nullptr;
	GetOwner()->ForceNetUpdate();
}

void UExtractionAbilities::Damaged()
{
	if (IsValid(Rescuer)) Rescuer->GetExtractionAbilities()->Cancel();
	Cancel();
}

void UExtractionAbilities::Complete()
{
	const auto* R = GetDefault<UExtractionRules>();
	const double Now = GetWorld()->GetTimeSeconds();
	if (Action == EExtractionAction::Revive)
	{
		auto* PS = Target->GetPlayerState<AExtractionPlayerState>();
		PS->Life = EExtractionLife::Alive;
		PS->BleedAt = 0;
		PS->DownHealth = 0;
		PS->ForceNetUpdate();
		Target->SetHealth(R->ReviveHealth);
		Target->SetShield(0);
		Target->UnCrouch();
		Target->UpdateHUDHealth();
		Target->ForceNetUpdate();
	}
	else if (Action == EExtractionAction::Heal)
	{
		Target->SetHealth(Target->GetHealth() + R->HealAmount);
		Target->UpdateHUDHealth();
		Target->ForceNetUpdate();
		HealReadyAt = Now + R->HealCooldown;
	}
	else if (Action == EExtractionAction::Smoke)
	{
		auto* Smoke = GetWorld()->SpawnActor<AExtractionSmoke>(SmokePoint, FRotator::ZeroRotator);
		if (Smoke) SmokeReadyAt = Now + R->SmokeCooldown;
	}
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_ABILITY Actor=%s Action=%d Completed"), *GetOwner()->GetName(),
	       static_cast<int32>(Action));
	Cancel();
}

void UExtractionAbilities::TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function)
{
	Super::TickComponent(Delta, Tick, Function);
	if (!GetOwner()->HasAuthority() || Action == EExtractionAction::None) return;
	const auto* C = Character();
	if (!CanStart() || !ValidTarget() || C->GetCharacterMovement()->IsFalling()
		|| C->GetVelocity().SizeSquared() > 25 || FVector::DistSquared(StartedAt, C->GetActorLocation()) > 4)
	{
		Cancel();
		return;
	}
	if (GetWorld()->GetTimeSeconds() >= ActionEnd) Complete();
}

void UExtractionAbilities::EndPlay(const EEndPlayReason::Type Reason)
{
	Damaged();
	Super::EndPlay(Reason);
}
