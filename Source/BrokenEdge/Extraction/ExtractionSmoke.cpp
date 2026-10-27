#include "ExtractionSmoke.h"
#include "ExtractionAbilities.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
// 烟雾 Actor 复制半径和到期时间，球体只提供视觉表现，不阻挡物理碰撞或导航。
// AI 视线和屏幕烟雾效果通过 Blocks 的几何查询判定，不能用渲染材质替代服务器遮挡规则。

// 烟团相对半径的垂直压缩：烟幕是贴地的团，不是等径球。
static constexpr float SmokeFlatten = .62f;

AExtractionSmoke::AExtractionSmoke()
{
	PrimaryActorTick.bCanEverTick = true;
	// 表现完全在本地推导，客户端也要进 Tick。
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	Cloud = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SmokeCloud"));
	SetRootComponent(Cloud);
	Cloud->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cloud->SetCanEverAffectNavigation(false);
	Cloud->SetCastShadow(false);
	Cloud->SetReceivesDecals(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
	if (Sphere.Succeeded()) Cloud->SetStaticMesh(Sphere.Object);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Game/Extraction/Abilities/M_ExtractionSmoke"));
	if (Material.Succeeded()) Cloud->SetMaterial(0, Material.Object);
}

void AExtractionSmoke::BeginPlay()
{
	Super::BeginPlay();
	LifetimeSeconds = GetDefault<UExtractionRules>()->SmokeSeconds;
	if (HasAuthority())
	{
		Radius = GetDefault<UExtractionRules>()->SmokeRadius;
		ExpiresAt = GetWorld()->GetTimeSeconds() + LifetimeSeconds;
		SetLifeSpan(LifetimeSeconds);
	}
	Refresh();
}

void AExtractionSmoke::SetTuning(float InRadius, float InSeconds)
{
	Radius = FMath::Max(InRadius, 50.f);
	LifetimeSeconds = FMath::Max(InSeconds, 1.f);
	if (HasAuthority()) ExpiresAt = GetWorld()->GetTimeSeconds() + LifetimeSeconds;
	Refresh();
}

// 外观随时间推进：先鼓胀到全尺寸，再在到期前淡出。
void AExtractionSmoke::Tick(float Delta)
{
	Super::Tick(Delta);
	if (BornAt < 0.f) BornAt = GetWorld()->GetTimeSeconds();
	ApplyLook();
}

void AExtractionSmoke::ApplyLook()
{
	if (!Cloud) return;
	const double Now = GetWorld()->GetTimeSeconds();
	const float Age = FMath::Max(static_cast<float>(Now - BornAt), 0.f);
	const float FadeOut = FMath::Min(FadeOutSeconds, LifetimeSeconds * .35f);
	// 鼓胀比淡入快：半径先到位，再提高浓度，避免相机周围先起雾而看不到烟团本身。
	const float Grow = FMath::Clamp(Age / FMath::Max(FadeInSeconds * .45f, .01f), 0.f, 1.f);
	const float Inflate = FMath::InterpEaseOut(.35f, 1.f, Grow, 2.f);
	// 不透明度：淡入线性、淡出平滑，避免消失时突然截断。
	const float FadeIn = FMath::Clamp(Age / FMath::Max(FadeInSeconds, .01f), 0.f, 1.f);
	const float Remaining = LifetimeSeconds - Age;
	const float FadeOutAlpha = FMath::Clamp(Remaining / FMath::Max(FadeOut, .01f), 0.f, 1.f);
	const float Alpha = FMath::Square(FMath::Min(FadeIn, FadeOutAlpha));
	// 半径按当前鼓胀比例缩放，垂直方向压缩成贴地烟团。
	const float Scale = Radius / 50.f * Inflate;
	Cloud->SetWorldScale3D(FVector(Scale, Scale, Scale * SmokeFlatten));
	if (!FMath::IsNearlyEqual(Alpha, AppliedAlpha, .004f))
	{
		AppliedAlpha = Alpha;
		if (auto* Dynamic = Cast<UMaterialInstanceDynamic>(Cloud->GetMaterial(0)))
			Dynamic->SetScalarParameterValue(TEXT("Opacity"), Alpha);
	}
}

// 基础球半径为 50 厘米，因此用 Radius/50 缩放；这是复制半径的表现回调。
void AExtractionSmoke::Refresh()
{
	// 首次刷新时把材质提升为动态实例，否则无法按帧调整不透明度。
	if (Cloud && !Cast<UMaterialInstanceDynamic>(Cloud->GetMaterial(0)))
	{
		if (auto* Source = Cloud->GetMaterial(0))
			Cloud->SetMaterial(0, UMaterialInstanceDynamic::Create(Source, this));
	}
	// 客户端在没有本地时间轴时也要有可见尺寸，立即按满尺寸摆放一次。
	AppliedAlpha = -1.f;
	ApplyLook();
}

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
	// 烟团垂直压缩，几何判定也跟着压扁，避免站在烟外却因为球的顶点被判定在烟内。
	const FVector Scale(1.f, 1.f, 1.f / SmokeFlatten);
	for (TActorIterator<AExtractionSmoke> It(World); It; ++It)
	{
		if (Now >= It->ExpiresAt) continue;
		const FVector A = (Start - It->GetActorLocation()) * Scale;
		const FVector B = (End - It->GetActorLocation()) * Scale;
		if (FMath::PointDistToSegmentSquared(FVector::ZeroVector, A, B) < FMath::Square(It->Radius)) return true;
	}
	return false;
}

// 相机在烟内返回 0..1 的相对深度；越靠近烟心越浓，用于全屏蒙版的渐隐。
float AExtractionSmoke::Haze(const UWorld* World, const FVector& Eye)
{
	if (!World) return 0.f;
	const auto* GS = World->GetGameState();
	const double Now = GS ? GS->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	const FVector Scale(1.f, 1.f, 1.f / SmokeFlatten);
	float Depth = 0.f;
	for (TActorIterator<AExtractionSmoke> It(World); It; ++It)
	{
		if (Now >= It->ExpiresAt) continue;
		const float Radius = FMath::Max(It->Radius, 1.f);
		// 用压扁后的距离计算浓度，和 Blocks 的几何保持一致。
		const float Flat = static_cast<float>(((Eye - It->GetActorLocation()) * Scale).Size());
		Depth = FMath::Max(Depth, FMath::Clamp(1.f - Flat / Radius, 0.f, 1.f));
	}
	return Depth;
}
