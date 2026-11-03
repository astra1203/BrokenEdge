#include "ExtractionBrokenBridge.h"
#include "Components/InstancedStaticMeshComponent.h"
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
	if (!HasAuthority() || Spans.IsEmpty()) return;
	int32 Forced = -1;
	BrokenSpan = FParse::Value(FCommandLine::Get(), TEXT("ExtractionBrokenBridge="), Forced) && Spans.IsValidIndex(Forced)
		             ? Forced
		             : FMath::RandRange(0, Spans.Num() - 1);
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_BROKEN_BRIDGE %d %s"), BrokenSpan, *Spans[BrokenSpan].Label.ToString());
	Apply();
	ForceNetUpdate();
}

void AExtractionBrokenBridge::OnRep_BrokenSpan() { Apply(); }

void AExtractionBrokenBridge::Apply()
{
	if (bApplied || !Spans.IsValidIndex(BrokenSpan)) return;
	bApplied = true;
	for (const auto& Part : Spans[BrokenSpan].Parts)
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
				Transform.SetScale3D(FVector(.001f));
				Transform.AddToTranslation(FVector(0, 0, -100000));
				Instances->SetMobility(EComponentMobility::Movable);
				Instances->UpdateInstanceTransform(Part.Instance, Transform, true, true, true);
			}
			else if (auto* Primitive = Cast<UPrimitiveComponent>(Component))
			{
				Primitive->SetVisibility(false);
				Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
	}
}
