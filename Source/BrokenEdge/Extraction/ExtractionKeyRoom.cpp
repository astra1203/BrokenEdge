#include "ExtractionKeyRoom.h"
#include "ExtractionInteractionComponent.h"
#include "ExtractionInventoryComponent.h"
#include "ExtractionItems.h"
#include "ExtractionLootActor.h"
#include "ExtractionAbilities.h"
#include "ExtractionState.h"
#include "ExtractionGameMode.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

// 一次性钥匙房：服务器验证背包版本并消耗钥匙，先发布解锁，再在开门时刻取消通行阻挡。
// 门动画是复制时刻驱动的表现；宝箱搜索还会检查 Open，不能仅看门的可见位置。

namespace
{
	/** 航天基地门扇（与关卡内既有门同源）；改动需同步 place_key_rooms.py 的说明。 */
	const TCHAR* DefaultDoorMeshPath = TEXT(
		"/Game/Maps/SpaceCenter/Assets/0f02cdffc1c62311/SM_H_Small_Door_11/StaticMeshes/SM_H_Small_Door_11.SM_H_Small_Door_11");

	/** 已摆进关卡的旧门没有序列化模型，这里兜底加载，避免出现隐形门。 */
	UStaticMesh* LoadDefaultDoorMesh()
	{
		static TWeakObjectPtr<UStaticMesh> Cached;
		if (!Cached.IsValid()) Cached = LoadObject<UStaticMesh>(nullptr, DefaultDoorMeshPath);
		return Cached.Get();
	}
}

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
	// 门扇本身就是碰撞体：关着时挡子弹、挡视线、挡角色，开门后整体交给 ApplyDoorState 关掉。
	DoorMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DoorMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	DoorMesh->SetGenerateOverlapEvents(false);
	DoorMesh->SetCanEverAffectNavigation(false);
	// 交互碰撞盒：只做范围检测，不阻挡移动/子弹，也不进导航。
	InteractionZone = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionZone"));
	InteractionZone->SetupAttachment(Barrier);
	InteractionZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionZone->SetCollisionObjectType(ECC_WorldDynamic);
	InteractionZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	InteractionZone->SetGenerateOverlapEvents(true);
	InteractionZone->SetCanEverAffectNavigation(false);
	InteractionZone->SetHiddenInGame(true);
#if WITH_EDITORONLY_DATA
	// 编辑器里画一个小图标，方便调交互盒大小（该字段只在编辑器数据下存在）。
	InteractionZone->bVisualizeComponent = true;
#endif
	// 构造期硬引用，保证打包时门扇模型一定被 cook 进包。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> DoorModel(DefaultDoorMeshPath);
	if (DoorModel.Succeeded()) DoorMesh->SetStaticMesh(DoorModel.Object);
}

void AExtractionKeyRoom::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// 编辑器里换模型或改门洞尺寸后立刻看到正确比例。
	FitDoorMesh();
	RefreshInteractionZone();
}

void AExtractionKeyRoom::RefreshInteractionZone()
{
	if (!InteractionZone || !Barrier) return;
	// 门洞尺寸为 0 时退回 Barrier 自身的盒体（旧关卡里的门没写 DoorSize）。
	const FVector Size = DoorSize.IsNearlyZero() ? Barrier->GetUnscaledBoxExtent() * 2.0 : DoorSize;
	InteractionZone->SetBoxExtent(FVector(
		FMath::Max(40.0, Size.X * 0.5 + InteractionZonePadding.X),
		FMath::Max(40.0, Size.Y * 0.5 + InteractionZonePadding.Y),
		FMath::Max(40.0, Size.Z * 0.5 + InteractionZonePadding.Z)));
	InteractionZone->SetRelativeLocation(InteractionZoneOffset);
}

bool AExtractionKeyRoom::IsInInteractionRange(const AActor* Other) const
{
	if (!InteractionZone || !Other) return false;
	if (InteractionZone->IsOverlappingActor(Other)) return true;
	// 重叠事件在客户端/服务器先后到达，几何判定兜底，避免"站进门里但提示不出来"。
	const FVector Local = GetActorTransform().InverseTransformPosition(Other->GetActorLocation());
	const FVector Extent = InteractionZone->GetUnscaledBoxExtent();
	const FVector Center = InteractionZone->GetRelativeLocation();
	return FMath::Abs(Local.X - Center.X) <= Extent.X
		&& FMath::Abs(Local.Y - Center.Y) <= Extent.Y
		&& FMath::Abs(Local.Z - Center.Z) <= Extent.Z;
}

void AExtractionKeyRoom::FitDoorMesh()
{
	if (!DoorMesh) return;
	if (!DoorMesh->GetStaticMesh() && bUseDefaultDoorMesh) DoorMesh->SetStaticMesh(LoadDefaultDoorMesh());
	const UStaticMesh* Mesh = DoorMesh->GetStaticMesh();
	if (!Mesh) return;
	if (!bFitDoorMesh) return;
	// 目标尺寸默认跟随门洞，模型原始尺寸差异不会造成穿墙或悬空。
	FVector Target = DoorSize.IsNearlyZero() ? Barrier->GetUnscaledBoxExtent() * 2.0 : DoorSize;
	const FVector ActorScale = GetActorScale3D().GetAbs();
	Target = FVector(Target.X / FMath::Max(ActorScale.X, 1.e-4), Target.Y / FMath::Max(ActorScale.Y, 1.e-4),
	                 Target.Z / FMath::Max(ActorScale.Z, 1.e-4));
	const FBoxSphereBounds Bounds = Mesh->GetBoundingBox();
	const FVector Native = Bounds.BoxExtent * 2.0;
	auto Axis = [](double Wanted, double Source)
	{
		return Wanted > 1.e-4 && Source > 1.e-4 ? Wanted / Source : 1.0;
	};
	// 只按门洞的宽、高做等比缩放；厚度沿用模型自身比例，免得门扇被压成一块砖。
	const double Ratio = FMath::Min(Axis(Target.X, Native.X), Axis(Target.Z, Native.Z));
	const FVector Scale(Ratio);
	DoorMesh->SetRelativeScale3D(Scale);
	// 模型轴心常在底面或门轴处，按包围盒中心重新对齐到门洞中心。
	DoorMesh->SetRelativeLocation(DoorMeshOffset - FVector(Bounds.Origin) * Scale);
}

void AExtractionKeyRoom::ApplyDoorState(bool bOpen)
{
	Barrier->SetCollisionEnabled(bOpen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	DoorMesh->SetCollisionEnabled(!bOpen && bDoorBlocks
		? ECollisionEnabled::QueryAndPhysics
		: ECollisionEnabled::NoCollision);
}

// 记录关门相对位置并刷新表现；服务器生成一次机密宝箱和文件来源批次，供撤离任务核验。
void AExtractionKeyRoom::BeginPlay()
{
	Super::BeginPlay();
	// 先套模型与比例，再记录关闭位置，滑门动画才有正确起点。
	FitDoorMesh();
	RefreshInteractionZone();
	ClosedLocation = DoorMesh->GetRelativeLocation();
	// 客户端也要拿到房名与卡名，交互提示才显示得出来。
	RequiredCardLabel = NSLOCTEXT("Extraction", "LegacyKey", "一次性钥匙");
	if (!RequiredKey.IsNone())
		for (const FName Id : UExtractionItemDefinition::Ids())
			if (const auto* D = UExtractionItemDefinition::Get(Id); D && D->KeyRoomId == RequiredKey)
			{
				RequiredCardLabel = D->Label;
				break;
			}
	OnRep_Door();
	if (!HasAuthority()) return;
	Cache = AExtractionLootActor::SpawnContainer(GetWorld(), GetActorTransform().TransformPosition(LootOffset), false);
	if (!Cache) return;
	Cache->AccessDoor = this;
	Cache->Label = CacheLabel;
	FExtractionContainer Contents;
	Contents.Id = FGuid::NewGuid();
	// 档位越高，机密文件与电子件越多：0 → 1/2/1，1 → 2/3/1，2 → 3/4/2，3 → 4/5/2。
	const int32 Documents = 1 + LootTier;
	const int32 Electronics = 2 + LootTier;
	const int32 Tools = 1 + LootTier / 2;
	for (int32 I = 0; I < Documents; ++I)
	{
		auto File = ExtractionItems::Make(TEXT("Document"));
		for (const auto& O : File.Origins) FileLots.Add(O.Lot);
		ExtractionItems::AutoPlace(Contents, File);
	}
	ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(TEXT("Electronics"), Electronics));
	for (int32 I = 0; I < Tools; ++I) ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(TEXT("Tools")));
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
	// 距离判定交给交互碰撞盒：站进盒子（约门宽外扩 65、门前 1.1 米）才算够得着。
	if (!C || C->GetWorld() != GetWorld() || !IsInInteractionRange(C)) return false;
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
	int32 Index = INDEX_NONE;
	if (RequiredKey.IsNone())
	{
		// 旧的一次性钥匙：整件消耗。
		Index = Candidate.Items.IndexOfByPredicate([](const auto& Item)
		{
			return Item.Definition == TEXT("Key") && Item.Slot < 0;
		});
		if (Index == INDEX_NONE) return TEXT("需要携带一次性钥匙");
		Candidate.Items.RemoveAt(Index);
	}
	else
	{
		// 同一房号的钥匙卡：优先用剩余次数最多的那张，次数用尽才移除整卡。
		for (int32 It = 0; It < Candidate.Items.Num(); ++It)
		{
			const auto& Item = Candidate.Items[It];
			if (Item.Slot >= 0) continue;
			const auto* D = UExtractionItemDefinition::Get(Item.Definition);
			if (!D || D->KeyRoomId != RequiredKey) continue;
			if (Index == INDEX_NONE || Item.Durability > Candidate.Items[Index].Durability) Index = It;
		}
		if (Index == INDEX_NONE)
			return FString::Printf(TEXT("需要携带%s"), *RequiredCardLabel.ToString());
		const int32 Uses = FMath::Max(1, FMath::RoundToInt(Candidate.Items[Index].Durability));
		// 开门扣一次耐久；扣到 0 就把卡整张吃掉，避免留下 0 次数的空卡。
		if (Uses <= 1) Candidate.Items.RemoveAt(Index);
		else Candidate.Items[Index].Durability = float(Uses - 1);
		UE_LOG(LogTemp, Display, TEXT("EXTRACTION_KEYCARD_CONSUMED Player=%s Room=%s Remaining=%d"),
		       *C->GetName(), *RequiredKey.ToString(), Uses <= 1 ? 0 : Uses - 1);
	}
	++Candidate.Revision;
	FString Error;
	if (!ExtractionItems::Validate(Candidate, Error)) return Error;
	Inventory->Contents = MoveTemp(Candidate);
	Inventory->Changed();
	Unlocked = true;
	OpenAt = GetWorld()->GetGameState()->GetServerWorldTimeSeconds() + .8;
	ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_KEYROOM_UNLOCKED Player=%s Room=%s"), *C->GetName(),
	       RequiredKey.IsNone() ? TEXT("LegacyKey") : *RequiredKey.ToString());
	return RequiredKey.IsNone() ? TEXT("钥匙已消耗 · 门已解锁") : TEXT("钥匙卡已使用 · 门已解锁");
}

// 以 Open 控制阻挡，并在已打开时直接对齐最终模型位置，支持晚收到状态的客户端。
void AExtractionKeyRoom::OnRep_Door()
{
	ApplyDoorState(Open);
	DoorMesh->SetRelativeLocation(Open ? ClosedLocation + Slide : ClosedLocation);
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

FString AExtractionKeyRoom::PromptText() const
{
	const FString Room = RoomLabel.IsEmpty() ? FString(TEXT("上锁房间")) : RoomLabel.ToString();
	const FText Card = RequiredCardLabel.IsEmpty() ? FText::FromString(TEXT("对应钥匙卡")) : RequiredCardLabel;
	return FString::Printf(TEXT("%s · 需要%s"), *Room, *Card.ToString());
}
