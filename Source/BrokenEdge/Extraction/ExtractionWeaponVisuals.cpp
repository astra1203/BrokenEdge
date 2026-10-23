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
	uint32 Layout = W->ExtractionConfiguration;
	for (const auto& Node : W->ExtractionAssembly)
	{
		Layout = HashCombine(Layout, GetTypeHash(Node.Id)); Layout = HashCombine(Layout, GetTypeHash(Node.Parent));
		Layout = HashCombine(Layout, GetTypeHash(Node.Socket));
	}
	if (Built != W->ExtractionParts || ViewParent.Get() != View || BuiltLayout != Layout)
	{
		Clear();
		Built = W->ExtractionParts; BuiltLayout = Layout;
		ViewParent = View;
		auto Create = [&](UStaticMesh* Mesh, USceneComponent* Parent, const FTransform& Mount, bool First, FName Socket = NAME_None) -> UStaticMeshComponent*
		{
			if (!Parent) return nullptr;
			auto* Part = NewObject<UStaticMeshComponent>(W);
			Part->SetStaticMesh(Mesh);
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->SetCanEverAffectNavigation(false);
			Part->SetupAttachment(Parent, Socket);
			Part->SetRelativeTransform(Mount);
			Part->SetOnlyOwnerSee(First);
			if (First)
			{
				Part->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
				Part->SetCastShadow(false);
			}
			Part->RegisterComponent();
			(First ? ViewParts : WorldParts).Add(Part);
			return Part;
		};
		if (!W->ExtractionAssembly.IsEmpty())
		{
			TMap<FGuid, UStaticMeshComponent*> WorldNodes, ViewNodes;
			TSet<FGuid> Finished;
			for (int32 Pass = 0; Pass < W->ExtractionAssembly.Num(); ++Pass)
				for (const auto& Node : W->ExtractionAssembly)
				{
					if (Finished.Contains(Node.Id) || (Node.Parent.IsValid() && !Finished.Contains(Node.Parent))) continue;
					const auto* D = UExtractionItemDefinition::Get(Node.Definition);
					if (!D) continue;
					USceneComponent* WorldHost = Node.Parent.IsValid() ? static_cast<USceneComponent*>(WorldNodes.FindRef(Node.Parent)) : W->GetWeaponMesh();
					USceneComponent* ViewHost = Node.Parent.IsValid() ? static_cast<USceneComponent*>(ViewNodes.FindRef(Node.Parent)) : View;
					WorldNodes.Add(Node.Id, Create(D->WorldMesh, WorldHost, D->Mount, false, Node.Socket));
					ViewNodes.Add(Node.Id, Create(D->FirstPersonMesh, ViewHost, D->FirstPersonMount, true, Node.Socket));
					if (D->AdapterMesh)
					{
						Create(D->AdapterMesh, WorldHost, D->AdapterMount, false, Node.Socket);
						Create(D->AdapterMesh, ViewHost, D->AdapterMount, true, Node.Socket);
					}
					Finished.Add(Node.Id);
				}
		}
		else
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
