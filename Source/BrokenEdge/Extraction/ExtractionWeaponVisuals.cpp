#include "ExtractionWeaponVisuals.h"
#include "ExtractionItems.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
// 武器附件的本地视觉装配：读取已复制的配置，分别创建世界模型与本人第一人称模型。
// 专服跳过外观创建；这些组件不参与伤害、库存或持久化，销毁表现不会消耗附件物品。

// 逐帧检查配置与第一人称父组件是否变化，仅发生变化时重建。
UExtractionWeaponVisuals::UExtractionWeaponVisuals()
{
	PrimaryComponentTick.bCanEverTick = true;
}

// 销毁已生成的两套附件组件并清空缓存，使下一次刷新能按新父对象重建。
void UExtractionWeaponVisuals::Clear()
{
	for (const auto& Part : WorldParts) if (IsValid(Part)) Part->DestroyComponent();
	for (const auto& Part : ViewParts) if (IsValid(Part)) Part->DestroyComponent();
	WorldParts.Reset();
	ViewParts.Reset();
	Built.Reset();
	ViewParent.Reset();
}

// 武器结束生命周期时释放全部动态附件，再调用父类清理。
void UExtractionWeaponVisuals::EndPlay(const EEndPlayReason::Type Reason)
{
	Clear();
	Super::EndPlay(Reason);
}

// 比较配件列表、装配布局和视角父组件；父件先于子件创建，随后按视角更新可见性。
void UExtractionWeaponVisuals::TickComponent(float Delta, ELevelTick Kind, FActorComponentTickFunction* Tick)
{
	Super::TickComponent(Delta, Kind, Tick);
	auto* W = Cast<AWeapon>(GetOwner());
	if (!W || GetNetMode() == NM_DedicatedServer) return;
	auto* C = Cast<ABrokenEdgeCharacter>(W->GetOwner());
	auto* View = C && C->GetEquippedWeapon() == W && C->IsLocallyControlled() ? C->GetFirstPersonWeaponMesh() : nullptr;
	uint32 Layout = HashCombine(W->ExtractionConfiguration, GetTypeHash(W->ExtractionDefinition));
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
			// 每轮只处理父件已创建的节点，最多按节点数遍历；非法树应先由服务器装备校验拒绝。
			TSet<FGuid> Finished;
			for (int32 Pass = 0; Pass < W->ExtractionAssembly.Num(); ++Pass)
				for (const auto& Node : W->ExtractionAssembly)
				{
					if (Finished.Contains(Node.Id) || (Node.Parent.IsValid() && !Finished.Contains(Node.Parent))) continue;
					const auto* D = UExtractionItemDefinition::Get(Node.Definition);
					if (!D) continue;
					USceneComponent* WorldHost = Node.Parent.IsValid() ? static_cast<USceneComponent*>(WorldNodes.FindRef(Node.Parent)) : W->GetWeaponMesh();
					USceneComponent* ViewHost = Node.Parent.IsValid() ? static_cast<USceneComponent*>(ViewNodes.FindRef(Node.Parent)) : View;
					WorldNodes.Add(Node.Id, Create(D->WorldMesh, WorldHost, D->Placement(W->ExtractionDefinition, false), false, Node.Socket));
					ViewNodes.Add(Node.Id, Create(D->FirstPersonMesh, ViewHost, D->Placement(W->ExtractionDefinition, true), true, Node.Socket));
					if (UStaticMesh* Adapter = D->AdapterFor(W->ExtractionDefinition))
					{
						Create(Adapter, WorldHost, D->AdapterMount, false, Node.Socket);
						Create(Adapter, ViewHost, D->AdapterMount, true, Node.Socket);
					}
					Finished.Add(Node.Id);
				}
		}
		else
		for (FName Name : Built)
			if (const auto* D = UExtractionItemDefinition::Get(Name))
			{
				Create(D->WorldMesh, W->GetWeaponMesh(), D->Placement(W->ExtractionDefinition, false), false);
				Create(D->FirstPersonMesh, View, D->Placement(W->ExtractionDefinition, true), true);
				Create(D->AdapterFor(W->ExtractionDefinition), W->GetWeaponMesh(), D->AdapterMount, false);
				Create(D->AdapterFor(W->ExtractionDefinition), View, D->AdapterMount, true);
			}
	}
	// 世界附件对第一人称拥有者隐藏，本人专用附件只在其第一人称视角显示，防止叠出两套模型。
	const bool First = C && C->IsLocallyControlled() && C->IsFirstPersonView() && !C->IsDefeated();
	for (const auto& Part : WorldParts)
		if (IsValid(Part))
		{
			Part->SetOwnerNoSee(First);
			Part->SetVisibility(W->GetWeaponMesh()->IsVisible());
		}
	for (const auto& Part : ViewParts) if (IsValid(Part)) Part->SetVisibility(First && View && View->IsVisible());
}

// 有握把配件时使用其支撑手修正，否则回退对应视角武器模型的 LeftHandSocket 组件空间变换。
FTransform AWeapon::EquipmentGrip(bool FirstPerson) const
{
	for (FName Name : ExtractionParts)
		if (const auto* D = UExtractionItemDefinition::Get(Name); D && D->AttachmentSlot
			== EExtractionAttachmentSlot::Grip)
			return D->GripFor(ExtractionDefinition);
	const auto* C = Cast<ABrokenEdgeCharacter>(GetOwner());
	const auto* Mesh = FirstPerson && C && C->GetFirstPersonWeaponMesh()
		                   ? C->GetFirstPersonWeaponMesh()
		                   : GetWeaponMesh();
	return Mesh->GetSocketTransform(TEXT("LeftHandSocket"), RTS_Component);
}

// 读取光学附件的瞄准偏移；没有瞄具附件时返回零偏移。
FVector AWeapon::EquipmentAimOffset() const
{
	for (FName Name : ExtractionParts)
		if (const auto* D = UExtractionItemDefinition::Get(Name); D && D->AttachmentSlot
			== EExtractionAttachmentSlot::Optic)
			return D->AimOffsetFor(ExtractionDefinition);
	return FVector::ZeroVector;
}
