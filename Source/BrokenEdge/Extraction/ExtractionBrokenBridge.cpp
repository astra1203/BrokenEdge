#include "ExtractionBrokenBridge.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

AExtractionBrokenBridge::AExtractionBrokenBridge()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AExtractionBrokenBridge::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionBrokenBridge, BrokenSpan);
}

void AExtractionBrokenBridge::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority() || Spans.IsEmpty() || Variants.IsEmpty()) return;
	int32 Forced = -1;
	BrokenSpan = FParse::Value(FCommandLine::Get(), TEXT("ExtractionBrokenBridge="), Forced) && Variants.IsValidIndex(Forced)
		             ? Forced
		             : FMath::RandRange(0, Variants.Num() - 1);
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_BROKEN_BRIDGE variant=%d mask=%d"), BrokenSpan, Variants[BrokenSpan]);
	Apply();
	ForceNetUpdate();
}

void AExtractionBrokenBridge::OnRep_BrokenSpan() { Apply(); }

void AExtractionBrokenBridge::Apply()
{
	if (bApplied || !Variants.IsValidIndex(BrokenSpan)) return;
	bApplied = true;
	int32 Debris = 0;
	for (int32 SpanIndex = 0; SpanIndex < Spans.Num(); ++SpanIndex)
	{
	if (!(Variants[BrokenSpan] & (1 << SpanIndex))) continue;
	for (const auto& Part : Spans[SpanIndex].Parts)
	{
		if (!IsValid(Part.Actor)) continue;
		for (UActorComponent* Component : Part.Actor->GetComponents())
		{
			if (!Part.Component.IsNone() && Component->GetFName() != Part.Component) continue;
			if (auto* Instances = Cast<UInstancedStaticMeshComponent>(Component); Instances && Part.Instance >= 0)
			{
				// 实例无法单独隐藏：缩到极小并沉到地下，渲染与碰撞一起消失，其他实例不受影响。
				FTransform Transform;
				if (!Instances->GetInstanceTransform(Part.Instance, Transform, true)) continue;
				SpawnCollapsed(Instances->GetStaticMesh(), Transform, Debris++);
				Transform.SetScale3D(FVector(.001f));
				Transform.AddToTranslation(FVector(0, 0, -100000));
				Instances->SetMobility(EComponentMobility::Movable);
				Instances->UpdateInstanceTransform(Part.Instance, Transform, true, true, true);
			}
			else if (auto* Primitive = Cast<UPrimitiveComponent>(Component))
			{
				if (auto* Mesh = Cast<UStaticMeshComponent>(Primitive))
					SpawnCollapsed(Mesh->GetStaticMesh(), Mesh->GetComponentTransform(), Debris++);
				Primitive->SetVisibility(false);
				Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
	}
	}
}

void AExtractionBrokenBridge::SpawnCollapsed(UStaticMesh* Mesh, const FTransform& Original, int32 Index)
{
	if (!Mesh) return;
	// 绕桥板一端下垂：先按模型包围盒找到较长水平轴的一端作为铰点，再绕横轴转 CollapsePitch 度；
	// 断法里相邻段交替朝两侧塌，看起来不像整齐复制。
	const FBox Bounds = Mesh->GetBoundingBox();
	const FVector Extent = Bounds.GetExtent();
	const bool bAlongX = Extent.X >= Extent.Y;
	const float Side = Index % 2 ? -1.f : 1.f;
	const FVector LocalHinge = Bounds.GetCenter() + (bAlongX ? FVector(Extent.X * Side, 0, Extent.Z)
		                                                      : FVector(0, Extent.Y * Side, Extent.Z));
	const FVector Axis = bAlongX ? FVector::YAxisVector : FVector::XAxisVector;
	const float Angle = FMath::DegreesToRadians(CollapsePitch) * (bAlongX ? Side : -Side);
	const FQuat Tilt(Axis, Angle);
	// 局部空间：平移到铰点 → 旋转 → 平移回去，再套原世界变换。
	const FTransform Local = FTransform(-LocalHinge) * FTransform(Tilt) * FTransform(LocalHinge);
	auto* Slab = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
	Slab->SetStaticMesh(Mesh);
	Slab->SetMobility(EComponentMobility::Movable);
	Slab->SetWorldTransform(Local * Original);
	Slab->SetCollisionProfileName(TEXT("BlockAll"));
	Slab->RegisterComponent();
	AddInstanceComponent(Slab);
}
