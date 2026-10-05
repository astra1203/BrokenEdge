#include "ExtractionKeyRoom.h"
#include "ExtractionInteractionComponent.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionLootActor.h"
#include "ExtractionAbilities.h"
#include "ExtractionState.h"
#include "ExtractionGameMode.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

AExtractionKeyRoom::AExtractionKeyRoom()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
	Barrier = CreateDefaultSubobject<UBoxComponent>(TEXT("Barrier"));
	SetRootComponent(Barrier);
	Barrier->SetBoxExtent(FVector(85, 15, 145));
	Barrier->SetCollisionProfileName(TEXT("BlockAll"));
	// Bake the doorway connection; the authority barrier blocks traversal until open.
	Barrier->SetCanEverAffectNavigation(false);
	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(Barrier);
	DoorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DoorMesh->SetCanEverAffectNavigation(false);
}

void AExtractionKeyRoom::BeginPlay()
{
	Super::BeginPlay();
	ClosedLocation = DoorMesh->GetRelativeLocation();
	OnRep_Door();
	if (!HasAuthority()) return;
	Cache = AExtractionLootActor::SpawnContainer(GetWorld(), GetActorTransform().TransformPosition(LootOffset), false);
	if (!Cache) return;
	Cache->AccessDoor = this;
	Cache->Label = TEXT("机密储藏箱");
	FExtractionContainer Contents;
	Contents.Id = FGuid::NewGuid();
	for (int32 I = 0; I < 3; ++I)
	{
		auto File = ExtractionItems::Make(TEXT("Document"));
		for (const auto& O : File.Origins) FileLots.Add(O.Lot);
		ExtractionItems::AutoPlace(Contents, File);
	}
	ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(TEXT("Electronics"), 4));
	for (int32 I = 0; I < 2; ++I) ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(TEXT("Tools")));
	Cache->SetContents(Contents);
}

void AExtractionKeyRoom::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionKeyRoom, Unlocked);
	DOREPLIFETIME(AExtractionKeyRoom, Open);
	DOREPLIFETIME(AExtractionKeyRoom, OpenAt);
	DOREPLIFETIME(AExtractionKeyRoom, Cache);
}

FVector AExtractionKeyRoom::Approach() const
{
	return GetActorLocation() + GetActorRotation().RotateVector(FVector(0, -120, -55));
}

bool AExtractionKeyRoom::CanReach(ABrokenEdgeCharacter* C) const
{
	if (!C || C->GetWorld() != GetWorld() || FVector::DistSquared(C->GetActorLocation(), GetActorLocation()) >
		FMath::Square(200.f))
		return false;
	FVector Eye;
	FRotator Rotation;
	C->GetActorEyesViewPoint(Eye, Rotation);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ExtractionDoor), false, C);
	if (C->GetEquippedWeapon()) Query.AddIgnoredActor(C->GetEquippedWeapon());
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, Eye, GetActorLocation(), ECC_Visibility, Query) || Hit.GetActor()
		== this;
}

FString AExtractionKeyRoom::Unlock(UExtractionInteractionComponent* I, int32 Revision)
{
	if (!HasAuthority() || !I || !I->CanAct() || !CanReach(I->Character())) return TEXT("无法开门：距离、视线或状态不满足");
	const auto* GM = GetWorld()->GetAuthGameMode<AExtractionGameMode>();
	auto* C = I->Character();
	if (!GM || !GM->IsCombatActive(C->GetController()) || I->InventoryOpen || I->MedicalEnd > 0
		|| C->GetCombatState() != ECombatState::ECS_Unoccuiped || C->GetExtractionAbilities()->Action !=
		EExtractionAction::None)
		return TEXT("当前动作不能开门");
	if (Unlocked) return TEXT("门已解锁，无需再次使用钥匙");
	auto* Inventory = I->Inventory();
	if (!Inventory || Inventory->Contents.Revision != Revision) return TEXT("背包已更新，请重试");
	FExtractionContainer Candidate = Inventory->Contents;
	const int32 Index = Candidate.Items.IndexOfByPredicate([](const auto& Item)
	{
		return Item.Definition == TEXT("Key") && Item.Slot < 0;
	});
	if (Index == INDEX_NONE) return TEXT("需要携带一次性钥匙");
	Candidate.Items.RemoveAt(Index);
	++Candidate.Revision;
	FString Error;
	if (!ExtractionItems::Validate(Candidate, Error)) return Error;
	Inventory->Contents = MoveTemp(Candidate);
	Inventory->Changed();
	Unlocked = true;
	OpenAt = GetWorld()->GetGameState()->GetServerWorldTimeSeconds() + .8;
	ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_KEYROOM_UNLOCKED Player=%s"), *C->GetName());
	return TEXT("钥匙已消耗 · 门已解锁");
}

void AExtractionKeyRoom::OnRep_Door()
{
	Barrier->SetCollisionEnabled(Open ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	if (Open) DoorMesh->SetRelativeLocation(ClosedLocation + Slide);
}

void AExtractionKeyRoom::Tick(float Delta)
{
	Super::Tick(Delta);
	if (!Unlocked || !GetWorld()->GetGameState()) return;
	const double Now = GetWorld()->GetGameState()->GetServerWorldTimeSeconds();
	DoorMesh->SetRelativeLocation(ClosedLocation + Slide * FMath::Clamp(float(1 - (OpenAt - Now) / .8), 0.f, 1.f));
	if (HasAuthority() && !Open && Now >= OpenAt)
	{
		Open = true;
		OnRep_Door();
		ForceNetUpdate();
	}
}
