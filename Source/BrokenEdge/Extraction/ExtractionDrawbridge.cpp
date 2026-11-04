#include "ExtractionDrawbridge.h"
#include "ExtractionExitSwitch.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

AExtractionDrawbridge::AExtractionDrawbridge()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = .03f;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Others = CreateDefaultSubobject<USceneComponent>(TEXT("Others"));
	Others->SetupAttachment(RootComponent);
	Others->SetRelativeLocation(FVector(-.464079, -.02123, -186.39844));
	// 官方类默认的两个转轴（DoorProxy1/2），竖起时分别 Roll +70 / -70。
	// 实测放平后桥面比两岸路面低 186 cm，正好是 Others 的 Z 偏移：转轴值本来就相对根节点，
	// 挂在 Others 下要把这段偏移加回去，桥面才和两岸齐平（probe_drawbridge.py）。
	constexpr double HingeLift = 186.39844;
	Hinge1 = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge1"));
	Hinge1->SetupAttachment(Others);
	Hinge1->SetRelativeLocation(FVector(1278, 123.61722, -490.76404 + HingeLift));
	Hinge2 = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge2"));
	Hinge2->SetupAttachment(Others);
	Hinge2->SetRelativeLocation(FVector(1278, -3646.8914, -492 + HingeLift));
	Deck1 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Deck1"));
	Deck1->SetupAttachment(Hinge1);
	Deck2 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Deck2"));
	Deck2->SetupAttachment(Hinge2);
	for (auto* Deck : {Deck1.Get(), Deck2.Get()})
	{
		Deck->SetMobility(EComponentMobility::Movable);
		Deck->SetCollisionProfileName(TEXT("BlockAll"));
	}
	// 关卡实例里两端挡墙的位置与缩放（BlockWallCube 为 100 cm 立方体）。
	Block1 = CreateDefaultSubobject<UBoxComponent>(TEXT("Block1"));
	Block1->SetupAttachment(Others);
	Block1->SetRelativeLocation(FVector(1280.4156, 381.9968, 490.143));
	Block1->SetBoxExtent(FVector(29.883867, 13.054422, 14.575291) * 50.f);
	Block2 = CreateDefaultSubobject<UBoxComponent>(TEXT("Block2"));
	Block2->SetupAttachment(Others);
	Block2->SetRelativeLocation(FVector(1277.5215, -3841.421, 411.00995));
	Block2->SetBoxExtent(FVector(29.986656, 12.60265, 14.857641) * 50.f);
	for (auto* Block : {Block1.Get(), Block2.Get()})
	{
		Block->SetCollisionProfileName(TEXT("BlockAllDynamic"));
		Block->SetCanEverAffectNavigation(true);
	}
	LockLocations = {FVector(1420.8071, 1263.987, 188.62305), FVector(1084.8718, -4668.064, 240.66992)};
	Label = NSLOCTEXT("Extraction", "Drawbridge", "东区吊桥");
}

void AExtractionDrawbridge::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionDrawbridge, LoweredAt);
}

void AExtractionDrawbridge::BeginPlay()
{
	Super::BeginPlay();
	ApplyPose();
	if (!HasAuthority()) return;
	for (const FVector& Local : LockLocations)
	{
		const FVector World = Others->GetComponentTransform().TransformPosition(Local);
		if (auto* Lock = GetWorld()->SpawnActor<AExtractionExitSwitch>(World, FRotator::ZeroRotator))
		{
			Lock->Bridge = this;
			Lock->ResetForRound(true);
			Locks.Add(Lock);
		}
	}
}

void AExtractionDrawbridge::Lower()
{
	if (!HasAuthority() || IsLowering()) return;
	LoweredAt = GetWorld()->GetTimeSeconds();
	// 另一端的锁也随之失效，避免放下后还能交互。
	for (AExtractionExitSwitch* Lock : Locks) if (IsValid(Lock)) Lock->ResetForRound(false);
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_DRAWBRIDGE_LOWER %s"), *GetName());
	ApplyPose();
	ForceNetUpdate();
}

void AExtractionDrawbridge::OnRep_LoweredAt() { ApplyPose(); }

void AExtractionDrawbridge::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsLowering()) ApplyPose();
}

void AExtractionDrawbridge::ApplyPose()
{
	float Alpha = 0.f;
	if (IsLowering())
	{
		const AGameStateBase* State = GetWorld()->GetGameState();
		const double Now = State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
		Alpha = FMath::Clamp(float((Now - LoweredAt) / LowerSeconds), 0.f, 1.f);
	}
	const float Eased = FMath::InterpEaseInOut(0.f, 1.f, Alpha, 2.f);
	Hinge1->SetRelativeRotation(FRotator(0, 0, RaisedRoll * (1.f - Eased)));
	Hinge2->SetRelativeRotation(FRotator(0, 0, -RaisedRoll * (1.f - Eased)));
	const bool bOpen = Alpha >= 1.f;
	for (auto* Block : {Block1.Get(), Block2.Get()})
		Block->SetCollisionEnabled(bOpen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	SetActorTickEnabled(IsLowering() && !bOpen);
}
