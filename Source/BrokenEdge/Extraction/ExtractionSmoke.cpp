#include "ExtractionSmoke.h"
#include "ExtractionAbilities.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
// 烟雾 Actor 复制半径和到期时间，球体只提供视觉表现，不阻挡物理碰撞或导航。
// AI 视线和屏幕烟雾效果通过 Blocks 的几何查询判定，不能用渲染材质替代服务器遮挡规则。

// 创建无碰撞球体并保持网络相关性，让烟雾状态可供各端查询。
AExtractionSmoke::AExtractionSmoke()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	Cloud = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SmokeCloud"));
	SetRootComponent(Cloud);
	Cloud->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cloud->SetCanEverAffectNavigation(false);
	Cloud->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
	if (Sphere.Succeeded()) Cloud->SetStaticMesh(Sphere.Object);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Game/Extraction/Abilities/M_ExtractionSmoke"));
	if (Material.Succeeded()) Cloud->SetMaterial(0, Material.Object);
}

// 服务器从规则设置半径、到期时刻和寿命，各端按当前半径刷新模型。
void AExtractionSmoke::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Radius = GetDefault<UExtractionRules>()->SmokeRadius;
		ExpiresAt = GetWorld()->GetTimeSeconds() + GetDefault<UExtractionRules>()->SmokeSeconds;
		SetLifeSpan(GetDefault<UExtractionRules>()->SmokeSeconds);
	}
	Refresh();
}

// 基础球半径为 50 厘米，因此用 Radius/50 缩放；这是复制半径的表现回调。
void AExtractionSmoke::Refresh() { Cloud->SetWorldScale3D(FVector(Radius / 50.f)); }

// 同步半径和绝对到期时间；客户端按同步服务器时间判断是否仍有效。
void AExtractionSmoke::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionSmoke, Radius);
	DOREPLIFETIME(AExtractionSmoke, ExpiresAt);
}

// 检测线段到有效烟雾球心的最短距离是否小于半径；起终点相同可用于判断相机是否在烟内。
bool AExtractionSmoke::Blocks(const UWorld* World, const FVector& Start, const FVector& End)
{
	if (!World) return false;
	const auto* GS = World->GetGameState();
	const double Now = GS ? GS->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	for (TActorIterator<AExtractionSmoke> It(World); It; ++It)
		if (Now < It->ExpiresAt && FMath::PointDistToSegmentSquared(It->GetActorLocation(), Start, End) <
			FMath::Square(It->Radius)) return true;
	return false;
}
