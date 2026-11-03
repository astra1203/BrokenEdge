#include "ExtractionWater.h"
#include "ExtractionExitSwitch.h"
#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AExtractionWater::AExtractionWater(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	bReplicates = true;
	bAlwaysRelevant = true;
	bWaterVolume = true;
	FluidFriction = .3f;
	Priority = 10;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	// 引擎先用体积根组件的包围盒粗筛，空刷子包围盒为零会被跳过，所以根组件换成真实范围的盒子。
	Extent = CreateDefaultSubobject<UBoxComponent>(TEXT("Extent"));
	Extent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Extent->SetCollisionResponseToAllChannels(ECR_Ignore);
	Extent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Extent->SetGenerateOverlapEvents(false);
	Extent->SetCanEverAffectNavigation(false);
	SetRootComponent(Extent);
	if (GetBrushComponent())
	{
		GetBrushComponent()->SetupAttachment(Extent);
		GetBrushComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
	Surface = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Surface"));
	Surface->SetupAttachment(Extent);
	if (Plane.Succeeded()) Surface->SetStaticMesh(Plane.Object);
	Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Surface->SetCastShadow(false);
	Surface->SetCanEverAffectNavigation(false);
}

void AExtractionWater::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionWater, FillStartedAt);
}

void AExtractionWater::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyLayout();
}

// 盒子覆盖池底到满水整段深度；水面平面单独按当前水位移动。
void AExtractionWater::ApplyLayout()
{
	const float Half = FMath::Max(1.f, (FullZ - BottomZ) * .5f);
	// 根组件不能挪位置（那会移动整个 Actor），盒子以 Actor 原点为中心，向上下各延伸到能盖住池底和水面。
	const float Reach = FMath::Max(FMath::Abs(BottomZ), FMath::Abs(FullZ));
	Extent->SetBoxExtent(FVector(HalfSize.X, HalfSize.Y, FMath::Max(Half, Reach)), true);
	if (SurfaceMaterial) Surface->SetMaterial(0, SurfaceMaterial);
	// 引擎平面是 100×100。
	Surface->SetRelativeScale3D(FVector(HalfSize.X / 50.f, HalfSize.Y / 50.f, 1.f));
	ApplySurface();
}

void AExtractionWater::BeginPlay()
{
	Super::BeginPlay();
	ApplyLayout();
	if (HasAuthority())
		for (const FVector& Local : SwitchLocations)
		{
			const FVector World = GetActorTransform().TransformPosition(Local);
			if (auto* Switch = GetWorld()->SpawnActor<AExtractionExitSwitch>(World, FRotator(0, GetActorRotation().Yaw, 0)))
			{
				Switch->Water = this;
				Switch->ResetForRound(!bStartsFull);
				Switches.Add(Switch);
			}
		}
	if (IsFilling()) SetActorTickEnabled(true);
}

double AExtractionWater::ServerNow() const
{
	const auto* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return State ? State->GetServerWorldTimeSeconds() : GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

float AExtractionWater::SurfaceZ() const
{
	if (bStartsFull) return FullZ;
	if (FillStartedAt < 0) return BottomZ;
	const float Alpha = FMath::Clamp(static_cast<float>((ServerNow() - FillStartedAt) / FMath::Max(.1f, FillSeconds)), 0.f, 1.f);
	return FMath::Lerp(BottomZ, FullZ, Alpha);
}

bool AExtractionWater::StartFill()
{
	if (!HasAuthority() || bStartsFull || IsFilling()) return false;
	FillStartedAt = ServerNow();
	ForceNetUpdate();
	OnRep_FillStartedAt();
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_WATER_FILL %s"), *GetName());
	return true;
}

void AExtractionWater::OnRep_FillStartedAt()
{
	ApplySurface();
	if (IsFilling()) SetActorTickEnabled(true);
}

void AExtractionWater::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ApplySurface();
	if (SurfaceZ() >= FullZ) SetActorTickEnabled(false);
}

void AExtractionWater::ApplySurface()
{
	const float Z = SurfaceZ();
	Surface->SetRelativeLocation(FVector(0, 0, Z));
	// 空池（或几乎没水）不显示水面，避免池底一层薄片闪烁。
	Surface->SetVisibility(Z > BottomZ + 2.f);
}

bool AExtractionWater::IsOverlapInVolume(const USceneComponent& TestComponent) const
{
	const float Z = SurfaceZ();
	if (Z <= BottomZ + 20.f) return false;
	const FVector Local = GetActorTransform().InverseTransformPosition(TestComponent.GetComponentLocation());
	// 胶囊中心略高于水面也算在水里，角色才能浮在水面而不是反复掉出水体。
	return FMath::Abs(Local.X) <= HalfSize.X && FMath::Abs(Local.Y) <= HalfSize.Y
		&& Local.Z <= Z + 15.f && Local.Z >= BottomZ - 100.f;
}
