#include "ExtractionWeaponVisuals.h"
#include "ExtractionItems.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"

UExtractionWeaponVisuals::UExtractionWeaponVisuals()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UExtractionWeaponVisuals::Clear()
{
	for (const auto& Part : WorldParts) if (IsValid(Part)) Part->DestroyComponent();
	for (const auto& Part : ViewParts) if (IsValid(Part)) Part->DestroyComponent();
	WorldParts.Reset();
	ViewParts.Reset();
	Built.Reset();
	ViewParent.Reset();
}

void UExtractionWeaponVisuals::EndPlay(const EEndPlayReason::Type Reason)
{
	Clear();
	Super::EndPlay(Reason);
}

void UExtractionWeaponVisuals::TickComponent(float Delta, ELevelTick Kind, FActorComponentTickFunction* Tick)
{
	Super::TickComponent(Delta, Kind, Tick);
	auto* W = Cast<AWeapon>(GetOwner());
	if (!W || GetNetMode() == NM_DedicatedServer) return;
	auto* C = Cast<ABrokenEdgeCharacter>(W->GetOwner());
	auto* View = C && C->GetEquippedWeapon() == W && C->IsLocallyControlled() ? C->GetFirstPersonWeaponMesh() : nullptr;
	if (Built != W->ExtractionParts || ViewParent.Get() != View)
	{
		Clear();
		Built = W->ExtractionParts;
		ViewParent = View;
		auto Create = [&](UStaticMesh* Mesh, USceneComponent* Parent, const FTransform& Mount, bool First)
		{
			if (!Mesh || !Parent) return;
			auto* Part = NewObject<UStaticMeshComponent>(W);
			Part->SetStaticMesh(Mesh);
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->SetCanEverAffectNavigation(false);
			Part->SetupAttachment(Parent);
			Part->SetRelativeTransform(Mount);
			Part->SetOnlyOwnerSee(First);
			if (First)
			{
				Part->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
				Part->SetCastShadow(false);
			}
			Part->RegisterComponent();
			(First ? ViewParts : WorldParts).Add(Part);
		};
		for (FName Name : Built)
			if (const auto* D = UExtractionItemDefinition::Get(Name))
			{
				Create(D->WorldMesh, W->GetWeaponMesh(), D->Mount, false);
				Create(D->FirstPersonMesh, View, D->FirstPersonMount, true);
				Create(D->AdapterMesh, W->GetWeaponMesh(), D->AdapterMount, false);
				Create(D->AdapterMesh, View, D->AdapterMount, true);
			}
	}
	const bool First = C && C->IsLocallyControlled() && C->IsFirstPersonView() && !C->IsDefeated();
	for (const auto& Part : WorldParts)
		if (IsValid(Part))
		{
			Part->SetOwnerNoSee(First);
			Part->SetVisibility(W->GetWeaponMesh()->IsVisible());
		}
	for (const auto& Part : ViewParts) if (IsValid(Part)) Part->SetVisibility(First && View && View->IsVisible());
}

FTransform AWeapon::EquipmentGrip(bool FirstPerson) const
{
	for (FName Name : ExtractionParts)
		if (const auto* D = UExtractionItemDefinition::Get(Name); D && D->AttachmentSlot
			== EExtractionAttachmentSlot::Grip)
			return D->SupportGrip;
	const auto* C = Cast<ABrokenEdgeCharacter>(GetOwner());
	const auto* Mesh = FirstPerson && C && C->GetFirstPersonWeaponMesh()
		                   ? C->GetFirstPersonWeaponMesh()
		                   : GetWeaponMesh();
	return Mesh->GetSocketTransform(TEXT("LeftHandSocket"), RTS_Component);
}

FVector AWeapon::EquipmentAimOffset() const
{
	for (FName Name : ExtractionParts)
		if (const auto* D = UExtractionItemDefinition::Get(Name); D && D->AttachmentSlot
			== EExtractionAttachmentSlot::Optic)
			return D->OpticAimOffset;
	return FVector::ZeroVector;
}
