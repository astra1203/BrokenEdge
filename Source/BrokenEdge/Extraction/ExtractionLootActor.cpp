#include "ExtractionLootActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"

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
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(RootComponent);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Box(TEXT(
		"/Game/Maps/SpaceCenter/Assets/409e475f1d6c58b5/SM_H_FutureTech_Tools_Box_01/StaticMeshes/SM_H_FutureTech_Tools_Box_01"));
	if (Box.Succeeded()) Mesh->SetStaticMesh(Box.Object);
}

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

void AExtractionLootActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionLootActor, Loose);
	DOREPLIFETIME(AExtractionLootActor, LooseDefinition);
	DOREPLIFETIME(AExtractionLootActor, Label);
	DOREPLIFETIME(AExtractionLootActor, Empty);
	DOREPLIFETIME(AExtractionLootActor, ContainerId);
}

void AExtractionLootActor::OnRep_Appearance()
{
	WeaponMesh->SetVisibility(false);
	Mesh->SetVisibility(true);
	if (Loose) InteractionBounds->SetBoxExtent(FVector(45, 25, 20));
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
				WeaponMesh->SetRelativeLocation(-Bounds.Origin + FVector(0, 0, Bounds.BoxExtent.Z - 20.f));
			}
			WeaponMesh->SetVisibility(true);
			Mesh->SetVisibility(false);
		}
	}
	if (UStaticMesh* Asset = Mesh->GetStaticMesh())
	{
		const float Scale = (Loose ? 34.f : 80.f) / FMath::Max(
			1.f, static_cast<float>(Asset->GetBounds().BoxExtent.GetMax() * 2));
		Mesh->SetRelativeScale3D(FVector(Scale));
		Mesh->SetRelativeLocation(-Asset->GetBounds().Origin * Scale
			+ FVector(0, 0, Asset->GetBounds().BoxExtent.Z * Scale - 20.f));
	}
}

FVector AExtractionLootActor::InteractionPoint() const { return InteractionBounds->GetComponentLocation(); }

void AExtractionLootActor::Changed()
{
	if (!HasAuthority()) return;
	Empty = Contents.Items.IsEmpty();
	ContainerId = Contents.Id;
	ForceNetUpdate();
	// Empty loose actors can be destroyed safely: a pending RPC still revalidates its session.
	if (Loose && Empty) SetLifeSpan(.5f);
}

void AExtractionLootActor::SetContents(const FExtractionContainer& NewContents)
{
	check(HasAuthority());
	Contents = NewContents;
	Changed();
}

AExtractionLootActor* AExtractionLootActor::SpawnContainer(UWorld* World, const FVector& Position,
                                                           bool bLoose, FName Definition)
{
	const FTransform Transform(FRotator::ZeroRotator, Position);
	auto* Loot = World->SpawnActorDeferred<AExtractionLootActor>(StaticClass(), Transform, nullptr, nullptr,
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
