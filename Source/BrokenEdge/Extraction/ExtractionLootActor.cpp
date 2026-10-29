#include "ExtractionLootActor.h"
#include "ExtractionGameMode.h"
#include "ExtractionKeyRoom.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"
// 战利品容器 Actor 只复制外观、标识与是否为空；完整 Contents 留在服务器。
// 每位查看者通过 InteractionComponent 获得自己的搜索视图，打开界面不会重新随机生成物资。

// 分离交互盒、容器阻挡盒和可见模型；模型不承担物品规则，碰撞负责视线与通行查询。
AExtractionLootActor::AExtractionLootActor()
{
	bReplicates = true;
	SetReplicateMovement(true);
	InteractionBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBounds"));
	SetRootComponent(InteractionBounds);
	InteractionBounds->SetBoxExtent(FVector(40, 30, 30));
	InteractionBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBounds->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBounds->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	ContainerBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("ContainerBounds"));
	ContainerBounds->SetupAttachment(RootComponent);
	ContainerBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ContainerBounds->SetCollisionResponseToAllChannels(ECR_Ignore);
	ContainerBounds->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	ContainerBounds->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(RootComponent);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

// 各端刷新外观，服务器初始化容器 ID；只有 SeedContents 开启且内容为空时才生成默认物资。
void AExtractionLootActor::BeginPlay()
{
	Super::BeginPlay();
	OnRep_Appearance();
	if (!HasAuthority()) return;
	if (!Contents.Id.IsValid()) Contents.Id = FGuid::NewGuid();
	if (SeedContents)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(ExtractionLootGround), false, this);
		for (TActorIterator<APawn> It(GetWorld()); It; ++It) Query.AddIgnoredActor(*It);
		for (TActorIterator<AWeapon> It(GetWorld()); It; ++It) Query.AddIgnoredActor(*It);
		for (TActorIterator<AExtractionLootActor> It(GetWorld()); It; ++It) Query.AddIgnoredActor(*It);
		FHitResult Ground;
		if (GetWorld()->LineTraceSingleByChannel(Ground, GetActorLocation() + FVector(0, 0, 100),
		                                         GetActorLocation() - FVector(0, 0, 400), ECC_Visibility, Query))
			SetActorLocation(Ground.ImpactPoint + FVector(0, 0, 20));
	}
	if (SeedContents && Contents.Items.IsEmpty())
	{
		if (Loose) ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(LooseDefinition, LooseQuantity));
		else if (EquipmentTier == 2 || EquipmentTier == 3)
		{
			for (const FName Id : {
				     FName(EquipmentTier == 2 ? TEXT("Helmet2") : TEXT("Helmet3")),
				     FName(EquipmentTier == 2 ? TEXT("Vest2") : TEXT("Vest3")), FName(TEXT("Bandage")),
				     FName(TEXT("Splint")),
				     FName(TEXT("RedDot")), FName(TEXT("VerticalGrip")), FName(TEXT("Compensator"))
			     })
				ensure(ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(Id)));
			ensure(ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(TEXT("AmmoAP"), 30)));
		}
		else
		{
			ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(TEXT("Ammo"), 60));
			ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(TEXT("Medkit")));
			ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(TEXT("Electronics"), 2));
			ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(TEXT("Tools")));
		}
	}
	Changed();
}

// 复制公开外观和门关联，不注册完整 Contents，避免泄露未搜索的具体物品。
void AExtractionLootActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionLootActor, Loose);
	DOREPLIFETIME(AExtractionLootActor, LooseDefinition);
	DOREPLIFETIME(AExtractionLootActor, Label);
	DOREPLIFETIME(AExtractionLootActor, Empty);
	DOREPLIFETIME(AExtractionLootActor, ContainerId);
	DOREPLIFETIME(AExtractionLootActor, SourcePointId);
	DOREPLIFETIME(AExtractionLootActor, ContainerAppearance);
	DOREPLIFETIME(AExtractionLootActor, AccessDoor);
}

// 依据整箱/散落物/枪械切换模型和查询边界；不移动作为根组件的交互盒，以免改变配置点位。
void AExtractionLootActor::OnRep_Appearance()
{
	Mesh->SetStaticMesh(DefaultContainerMesh);
	Mesh->SetRelativeTransform(FTransform::Identity);
	WeaponMesh->SetRelativeTransform(FTransform::Identity);
	InteractionBounds->SetBoxExtent(Loose ? LooseInteractionExtent : ContainerInteractionExtent);
	WeaponMesh->SetVisibility(false);
	Mesh->SetVisibility(true);
	ContainerBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InteractionBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	if (ContainerAppearance && !Loose)
	{
		WeaponMesh->SetSkeletalMeshAsset(ContainerAppearance);
		WeaponMesh->SetVisibility(true);
		Mesh->SetVisibility(false);
		const auto Bounds = ContainerAppearance->GetBounds();
		// The interaction box is the actor root; changing its location would move the authored point.
		InteractionBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ContainerBounds->SetRelativeLocation(Bounds.Origin + VisualOffset - FVector(0, 0, -20));
		ContainerBounds->SetBoxExtent(Bounds.BoxExtent.ComponentMax(FVector(5)));
		ContainerBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		WeaponMesh->SetRelativeLocation(VisualOffset - FVector(0, 0, -20));
		return;
	}
	if (const auto* Def = Loose ? UExtractionItemDefinition::Get(LooseDefinition) : nullptr)
	{
		if (Def->WorldMesh) Mesh->SetStaticMesh(Def->WorldMesh);
		if (Def->WeaponClass)
		{
			WeaponMesh->SetSkeletalMeshAsset(
				Def->WeaponClass.GetDefaultObject()->GetWeaponMesh()->GetSkeletalMeshAsset());
			if (const auto* Asset = WeaponMesh->GetSkeletalMeshAsset())
			{
				const auto Bounds = Asset->GetBounds();
				WeaponMesh->SetRelativeLocation(-Bounds.Origin + FVector(0, 0, Bounds.BoxExtent.Z) + VisualOffset);
			}
			WeaponMesh->SetVisibility(true);
			Mesh->SetVisibility(false);
		}
	}
	if (UStaticMesh* Asset = Mesh->GetStaticMesh())
	{
		const float Scale = (Loose ? LooseSize : ContainerSize) / FMath::Max(
			1.f, static_cast<float>(Asset->GetBounds().BoxExtent.GetMax() * 2));
		Mesh->SetRelativeScale3D(FVector(Scale));
		Mesh->SetRelativeLocation(-Asset->GetBounds().Origin * Scale
			+ FVector(0, 0, Asset->GetBounds().BoxExtent.Z * Scale) + VisualOffset);
	}
}

// 返回容器外观中心的世界位置或交互盒位置，供距离和视线判断使用。
FVector AExtractionLootActor::InteractionPoint() const
{
	return ContainerAppearance
		       ? GetActorTransform().TransformPosition(
			       ContainerAppearance->GetBounds().Origin + VisualOffset - FVector(0, 0, -20))
		       : InteractionBounds->GetComponentLocation();
}

// 服务器同步空状态与容器 ID；空散落物延后销毁，旧 RPC 仍须重新校验对象与会话。
void AExtractionLootActor::Changed()
{
	if (!HasAuthority()) return;
	Empty = Contents.Items.IsEmpty();
	ContainerId = Contents.Id;
	ForceNetUpdate();
	// Empty loose actors can be destroyed safely: a pending RPC still revalidates its session.
	if (Loose && Empty) SetLifeSpan(.5f);
}

// 仅服务器替换容器内容并通知公开状态更新；搜索视图由查看者另行刷新。
void AExtractionLootActor::SetContents(const FExtractionContainer& NewContents)
{
	check(HasAuthority());
	Contents = NewContents;
	Changed();
}

// 延迟生成运行时掉落容器，关闭自动种子物资以免额外产出；返回空表示生成失败。
AExtractionLootActor* AExtractionLootActor::SpawnContainer(UWorld* World, const FVector& Position,
                                                           bool bLoose, FName Definition)
{
	const auto Class = ResolveSpawnClass(World);
	if (!Class) return nullptr;
	const FTransform Transform(FRotator::ZeroRotator, Position);
	auto* Loot = World->SpawnActorDeferred<AExtractionLootActor>(Class, Transform, nullptr, nullptr,
	                                                             ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Loot) return nullptr;
	Loot->Loose = bLoose;
	Loot->LooseDefinition = Definition;
	Loot->SeedContents = false;
	Loot->Label = bLoose ? TEXT("地面物品") : TEXT("死亡物资");
	Loot->FinishSpawning(Transform);
	Loot->SetLifeSpan(0);
	return Loot;
}

void AExtractionLootActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	OnRep_Appearance();
}

TSubclassOf<AExtractionLootActor> AExtractionLootActor::ResolveSpawnClass(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_Client) return nullptr;
	const auto* Mode = World->GetAuthGameMode<AExtractionGameMode>();
	if (!Mode || !Mode->LootActorClass || Mode->LootActorClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogTemp, Error, TEXT("EXTRACTION_LOOT_CLASS_INVALID"));
		return nullptr;
	}
	return Mode->LootActorClass;
}
