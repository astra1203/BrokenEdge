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
// 一次性钥匙房：服务器验证背包版本并消耗钥匙，先发布解锁，再在开门时刻取消通行阻挡。
// 门动画是复制时刻驱动的表现；宝箱搜索还会检查 Open，不能仅看门的可见位置。

// 创建权威阻挡盒与无碰撞门模型；门不改导航烘焙，是否可通行由阻挡盒控制。
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

// 记录关门相对位置并刷新表现；服务器生成一次机密宝箱和文件来源批次，供撤离任务核验。
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

// 同步解锁、完全打开、开门截止时间和宝箱引用；文件来源批次不向客户端公开。
void AExtractionKeyRoom::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionKeyRoom, Unlocked);
	DOREPLIFETIME(AExtractionKeyRoom, Open);
	DOREPLIFETIME(AExtractionKeyRoom, OpenAt);
	DOREPLIFETIME(AExtractionKeyRoom, Cache);
}

// 给出门前方的世界参考位置，用于接近该门的调用者，不代表已经通过距离检查。
FVector AExtractionKeyRoom::Approach() const
{
	return GetActorLocation() + GetActorRotation().RotateVector(FVector(0, -120, -55));
}

// 要求同世界、两米内且视线无遮挡；射线命中门本身允许交互。
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

// 服务器验证行动、存储、动作互斥和背包版本，再在副本中移除钥匙并校验。
// 成功后递增版本并设定 0.8 秒开门时刻；返回值是反馈文本，成功时也不是空字符串。
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

// 以 Open 控制阻挡，并在已打开时直接对齐最终模型位置，支持晚收到状态的客户端。
void AExtractionKeyRoom::OnRep_Door()
{
	Barrier->SetCollisionEnabled(Open ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	if (Open) DoorMesh->SetRelativeLocation(ClosedLocation + Slide);
}

// 使用同步服务器时间插值门模型；仅服务器在到时后发布 Open 并移除阻挡。
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
