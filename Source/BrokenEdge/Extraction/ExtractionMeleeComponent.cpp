#include "ExtractionMeleeComponent.h"
#include "ExtractionCombatComponent.h"
#include "ExtractionInteractionComponent.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "GameFramework/DamageType.h"

UExtractionMeleeComponent::UExtractionMeleeComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
}

ABrokenEdgeCharacter* UExtractionMeleeComponent::Character() const { return Cast<ABrokenEdgeCharacter>(GetOwner()); }

bool UExtractionMeleeComponent::CanAct() const
{
	const auto* C = Character();
	return C && C->GetExtractionCombat()->CanAct() && !C->GetExtractionInteraction()->InventoryOpen;
}

bool UExtractionMeleeComponent::Busy() const { return GetWorld() && GetWorld()->GetTimeSeconds() < NextAttack; }

void UExtractionMeleeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UExtractionMeleeComponent, Active);
}

void UExtractionMeleeComponent::Select_Implementation(bool Knife, bool SwapGun)
{
	auto* C = Character();
	if (!CanAct() || Busy() || !C->GetCombat() || C->GetCombatState() != ECombatState::ECS_Unoccuiped)
		return;
	C->GetExtractionInteraction()->StopCombatInput();
	C->GetExtractionInteraction()->CancelMedical();
	Active = Knife;
	if (!Knife && SwapGun) C->GetCombat()->SwapWeapons();
	C->ForceNetUpdate();
}

void UExtractionMeleeComponent::Attack_Implementation(bool Heavy)
{
	if (!Active || !CanAct() || Busy()) return;
	HeavyAttack = Heavy;
	// Prototype timing and damage are intentionally separate from source animation metadata.
	NextAttack = GetWorld()->GetTimeSeconds() + (Heavy ? 1.0 : .6);
	Character()->GetExtractionInteraction()->CancelMedical();
	GetWorld()->GetTimerManager().SetTimer(StrikeTimer, this, &ThisClass::Strike, Heavy ? .35f : .18f);
	PlayAttack(Heavy);
}

void UExtractionMeleeComponent::Strike()
{
	if (!Active || !CanAct()) return;
	auto* C = Character();
	FVector Start;
	FRotator Direction;
	C->GetActorEyesViewPoint(Start, Direction);
	const FVector End = Start + Direction.Vector() * (HeavyAttack ? 170.f : 150.f);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ExtractionKnife), false, C);
	if (C->GetEquippedWeapon()) Query.AddIgnoredActor(C->GetEquippedWeapon());
	FHitResult Hit;
	if (GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility,
	                                     FCollisionShape::MakeSphere(14), Query) && Hit.GetActor())
		UGameplayStatics::ApplyPointDamage(Hit.GetActor(), HeavyAttack ? 65.f : 35.f, Direction.Vector(), Hit,
		                                   C->GetController(), C, UDamageType::StaticClass());
}

void UExtractionMeleeComponent::PlayAttack_Implementation(bool Heavy)
{
	auto* C = Character();
	if (!C) return;
	if (auto* Anim = C->GetMesh()->GetAnimInstance())
		if (auto* Montage = Heavy ? HeavyBody.Get() : LightBody.Get()) Anim->Montage_Play(Montage);
	if (auto* View = C->GetFirstPersonMesh())
		if (auto* Anim = View->GetAnimInstance())
			if (auto* Montage = Heavy ? HeavyView.Get() : LightView.Get()) Anim->Montage_Play(Montage);
}

void UExtractionMeleeComponent::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function)
{
	Super::TickComponent(Delta, Type, Function);
	auto* C = Character();
	if (!C) return;
	if (C->HasAuthority() && Active && !C->GetExtractionCombat()->CanAct()) Active = false;
	auto* Gun = C->GetEquippedWeapon();
	if (HiddenGun.IsValid() && (HiddenGun != Gun || !Active)) HiddenGun->SetActorHiddenInGame(false);
	HiddenGun = Active ? Gun : nullptr;
	if (HiddenGun.IsValid()) HiddenGun->SetActorHiddenInGame(true);
	if (!Mesh) return;
	if (!WorldMesh)
	{
		WorldMesh = NewObject<USkeletalMeshComponent>(C);
		WorldMesh->SetupAttachment(C->GetMesh(), TEXT("RightHand"));
		WorldMesh->SetSkeletalMeshAsset(Mesh);
		WorldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WorldMesh->SetCanEverAffectNavigation(false);
		WorldMesh->SetRelativeTransform(HandOffset);
		WorldMesh->RegisterComponent();
	}
	WorldMesh->SetVisibility(Active && !C->IsDefeated());
	WorldMesh->SetOwnerNoSee(C->IsLocallyControlled() && C->IsFirstPersonView());
	if (!ViewMesh && C->GetFirstPersonMesh())
	{
		ViewMesh = NewObject<USkeletalMeshComponent>(C);
		ViewMesh->SetupAttachment(C->GetFirstPersonMesh(), TEXT("RightHand"));
		ViewMesh->SetSkeletalMeshAsset(Mesh);
		ViewMesh->SetOnlyOwnerSee(true);
		ViewMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
		ViewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ViewMesh->SetCastShadow(false);
		ViewMesh->SetCanEverAffectNavigation(false);
		ViewMesh->SetRelativeTransform(HandOffset);
		ViewMesh->RegisterComponent();
	}
	if (ViewMesh)
		ViewMesh->SetVisibility(
			Active && C->IsLocallyControlled() && C->IsFirstPersonView() && !C->IsDefeated());
}

void UExtractionMeleeComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorld()->GetTimerManager().ClearTimer(StrikeTimer);
	if (HiddenGun.IsValid()) HiddenGun->SetActorHiddenInGame(false);
	if (WorldMesh) WorldMesh->DestroyComponent();
	if (ViewMesh) ViewMesh->DestroyComponent();
	Super::EndPlay(Reason);
}
