#include "ExtractionSmoke.h"
#include "ExtractionAbilities.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
// 烟雾 Actor 复制半径和到期时间，球体只提供视觉表现，不阻挡物理碰撞或导航。
// AI 视线和屏幕烟雾效果通过 Blocks 的几何查询判定，不能用渲染材质替代服务器遮挡规则。

// 出生第一帧的不透明度下限。从 0 起淡入等于把"能不能看见"全押在 Tick 上：
// 只要 Tick 没跑起来或 Age 卡在 0，Alpha 就永远是 0，表现为烟完全不出现。
static constexpr float SmokeFadeInFloor = .7f;

// 叠层布局：相对半径的偏移（X,Y,Z 单位为半径比例）与自身尺寸比例。
// 一层薄壳在透明材质下会透出背景、看起来只剩一层灰雾，所以这里用一主多辅叠出体积。
struct FSmokePuffLayer
{
	FVector Offset;
	float Scale;
	float Alpha;
};

static const FSmokePuffLayer PuffLayers[] = {
	// 主团：恒在原点。它是根组件，偏移会被解释成世界坐标，非零就等于把烟幕搬走。
	{FVector::ZeroVector, 1.00f, 1.00f},
	// 辅团：偏心错开，填满主团边缘的透明衰减带。
	{FVector(.48f, .10f, .02f), .70f, .85f},
	{FVector(-.42f, .28f, .08f), .64f, .80f},
	{FVector(.06f, -.50f, .04f), .72f, .85f},
	// 顶层：略微抬高，让烟幕有向上翻起的轮廓，不是个扁饼。
	{FVector(-.20f, -.16f, .30f), .58f, .55f},
	{FVector(.26f, .34f, .26f), .50f, .50f},
};
static constexpr int32 PuffLayerCount = UE_ARRAY_COUNT(PuffLayers);

// 云团叠层布局。和球体那套不同，这里每层是一整朵云，只做"错位 + 转向 + 微缩"：
// 位置和缩放都按整团半径的比例给，换半径时不用改表。
//
// 分布是三层同心球壳而不是水平铺开：偏移带 Z 分量，每层尺寸随偏移增大而减小，
// 保证 |offset| + scale ≈ 1，叠完的轮廓是个球，不是一张扁饼。
// 16 层不透明度按 1-(1-a)^n 累积（单层约 0.22 → 总 0.98），这才读得出"实心烟团"。
// 三层分工：外壳定轮廓、中层填体积、内芯压实球心（壳层都贴外圈时球心反而最空）。
struct FSmokeCloudLayer
{
	FVector Offset;   // 相对半径的偏移；Z 分量让它成球，不再是贴地扁团
	float Yaw;        // 绕 Z 转向：打散精灵分布，避免看出每层是同一张云的复制
	float Scale;      // 相对整团半径的尺寸
};

static const FSmokeCloudLayer CloudLayerLayout[] = {
	// 骨架：铺满整个半径，整团轮廓由它定。
	{FVector::ZeroVector, 0.f, 1.00f},
	// 外壳 7 层：贴壳面分布，覆盖六个轴向和一条对角，把球面填满。
	{FVector(.30f, .00f, .00f), 23.f, .74f},
	{FVector(.00f, .30f, .00f), 45.f, .74f},
	{FVector(-.30f, .00f, .00f), 68.f, .74f},
	{FVector(.00f, -.30f, .00f), 90.f, .74f},
	{FVector(.00f, .00f, .32f), 113.f, .72f},
	{FVector(.00f, .00f, -.30f), 135.f, .74f},
	{FVector(.21f, .21f, .14f), 158.f, .74f},
	// 中层 5 层：半径收到一半左右，填补外壳与内芯之间的空腔。
	{FVector(-.21f, -.21f, -.14f), 180.f, .74f},
	{FVector(.16f, -.12f, .06f), 203.f, .90f},
	{FVector(-.14f, .14f, -.08f), 225.f, .90f},
	{FVector(.10f, .16f, .10f), 248.f, .88f},
	{FVector(-.16f, -.10f, .10f), 270.f, .88f},
	{FVector(.06f, -.18f, -.12f), 293.f, .90f},
	// 内芯 2 层：几乎不偏移、尺寸接近整团，把球心堆到实心。
	{FVector(.05f, -.05f, .06f), 315.f, .98f},
	{FVector(-.06f, .04f, -.05f), 338.f, .98f},
};
static_assert(UE_ARRAY_COUNT(CloudLayerLayout) == AExtractionSmoke::CloudLayerMax,
	"CloudLayerMax 必须和 CloudLayerLayout 的条目数一致");
static constexpr int32 CloudLayerSlots = UE_ARRAY_COUNT(CloudLayerLayout);

AExtractionSmoke::AExtractionSmoke()
{
	PrimaryActorTick.bCanEverTick = true;
	// 表现完全在本地推导，客户端也要进 Tick。
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	Cloud = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SmokeCloud"));
	SetRootComponent(Cloud);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Game/Extraction/Abilities/M_ExtractionSmoke"));
	// 主团和辅助团共用同一套网格与材质，只在偏移、尺寸和浓度上不同。
	TArray<UStaticMeshComponent*> All{Cloud};
	for (int32 Index = 0; Index < PuffLayerCount; ++Index)
	{
		if (Index > 0)
		{
			auto* Puff = CreateDefaultSubobject<UStaticMeshComponent>(
				*FString::Printf(TEXT("SmokePuff%d"), Index));
			Puff->SetupAttachment(Cloud);
			All.Add(Puff);
			Puffs.Add(Puff);
		}
		UStaticMeshComponent* Mesh = All.Last();
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetCastShadow(false);
		Mesh->SetReceivesDecals(false);
		if (Sphere.Succeeded()) Mesh->SetStaticMesh(Sphere.Object);
		if (Material.Succeeded()) Mesh->SetMaterial(0, Material.Object);
	}

	// 云团表现：挂在根组件下，跟着烟幕一起被摆放。默认资产来自闪电包里的大团云，
	// 它的颗粒物比球体叠层更蓬松；资产不存在时这里静默为空，退回球体。
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> DefaultCloud(
		TEXT("/Game/Lightning_Electricity_Pack/Particles/P_Clouds.P_Clouds"));
	CloudSystem = DefaultCloud.Object;
	for (int32 Index = 0; Index < CloudLayerSlots; ++Index)
	{
		auto* Layer = CreateDefaultSubobject<UNiagaraComponent>(
			*FString::Printf(TEXT("SmokeCloudFx%d"), Index));
		Layer->SetupAttachment(Cloud);
		// 云团只做表现：不投影、不参与任何碰撞或导航，遮挡判定始终走 Blocks 的几何查询。
		Layer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Layer->SetCanEverAffectNavigation(false);
		Layer->SetCastShadow(false);
		Layer->SetGenerateOverlapEvents(false);
		// 保留组件默认的自动播放：Niagara 组件最可靠的开播路径就是随组件注册自动激活。
		// 手动 Activate 在部分时机下会因为组件尚未就绪而静默失效，云团挂着却一颗粒子都没有。
		if (CloudSystem) Layer->SetAsset(CloudSystem);
		CloudFxLayers.Add(Layer);
	}
}

void AExtractionSmoke::BeginPlay()
{
	Super::BeginPlay();
	// 摆任何一层之前先记住它该在哪，后面用来检测宿主被搬走。
	SpawnLocation = GetActorLocation();
	bSpawnCaptured = true;
	LifetimeSeconds = GetDefault<UExtractionRules>()->SmokeSeconds;
	// 云团的外观参数全部走配置（Config/DefaultGame.ini 的 ExtractionRules 段）：
	// 调大小、层数、形状都只是改 ini 重启 PIE，不用重编译。
	const UExtractionRules* Rules = GetDefault<UExtractionRules>();
	CloudLayerCount = FMath::Clamp(Rules->SmokeCloudLayers, 1, CloudLayerMax);
	CloudSizeScale = FMath::Clamp(Rules->SmokeCloudSizeScale, .2f, 2.f);
	CloudAspect = FMath::Clamp(Rules->SmokeCloudAspect, .2f, 6.f);
	if (HasAuthority())
	{
		Radius = GetDefault<UExtractionRules>()->SmokeRadius;
		ExpiresAt = GetWorld()->GetTimeSeconds() + LifetimeSeconds;
		SetLifeSpan(LifetimeSeconds);
	}
	// 先定表现后端再刷外观：Refresh 里会按当前后端写尺寸和透明度。
	ApplyVisualBackend();
	Refresh();
}

// 球体叠层和 Niagara 云团是两套互斥的表现。同时开着会得到两层半透明，
// 颜色叠加后整体发白、边缘发灰，看起来比任何一套单独用都糟，所以这里是二选一。
void AExtractionSmoke::ApplyVisualBackend()
{
	const bool bWantCloud = bUseCloudFx && CloudSystem != nullptr && CloudFxLayers.Num() > 0;
	if (bWantCloud)
	{
		// 隐藏根组件时不能向下传播：云团就挂在它下面，一传播会被一起藏掉。
		if (Cloud) Cloud->SetVisibility(false, false);
		for (const TObjectPtr<UStaticMeshComponent>& Puff : Puffs)
			if (Puff) Puff->SetVisibility(false, false);
		// 只启用前 CloudLayerCount 层，多出来的直接关掉而不是销毁：
		// 层数是调试开关，改完不用重建 Actor。
		const int32 Active = FMath::Clamp(CloudLayerCount, 1, CloudFxLayers.Num());
		for (int32 Index = 0; Index < CloudFxLayers.Num(); ++Index)
		{
			UNiagaraComponent* Fx = CloudFxLayers[Index];
			if (!Fx) continue;
			if (Index >= Active)
			{
				Fx->Deactivate();
				Fx->SetAutoActivate(false);
				Fx->SetVisibility(false, true);
				continue;
			}
			Fx->SetVisibility(true, true);
			if (Fx->GetAsset() != CloudSystem) Fx->SetAsset(CloudSystem);
			if (!bCloudFxActive || !Fx->IsActive())
			{
				// 只调 Activate 不够：组件在构造里关掉了自动激活，某些注册时机下
				// Activate 会因为 bAutoActivate 为假而直接返回，表现就是云团挂着却
				// 一颗粒子都没有 —— 比完全没有云团更难查。先把自动激活打开再激活。
				Fx->SetAutoActivate(true);
				Fx->Activate(true);
			}
		}
		bCloudFxActive = true;
	}
	else
	{
		if (Cloud) Cloud->SetVisibility(true, false);
		for (const TObjectPtr<UStaticMeshComponent>& Puff : Puffs)
			if (Puff) Puff->SetVisibility(true, false);
		if (bCloudFxActive)
		{
			for (const TObjectPtr<UNiagaraComponent>& Fx : CloudFxLayers)
			{
				if (!Fx) continue;
				Fx->Deactivate();
				// 关掉自动激活，否则组件重新注册时又会自己冒出来一层云。
				Fx->SetAutoActivate(false);
				Fx->SetVisibility(false, true);
			}
			bCloudFxActive = false;
		}
	}
}

void AExtractionSmoke::SetTuning(float InRadius, float InSeconds)
{
	Radius = FMath::Max(InRadius, 50.f);
	LifetimeSeconds = FMath::Max(InSeconds, 1.f);
	if (HasAuthority())
	{
		ExpiresAt = GetWorld()->GetTimeSeconds() + LifetimeSeconds;
		// 生命周期必须跟 ApplyLook 用的寿命一致，否则烟会在还没淡完时被直接销毁。
		SetLifeSpan(LifetimeSeconds);
	}
	Refresh();
}

// 外观随时间推进：先鼓胀到全尺寸，再在到期前淡出。
void AExtractionSmoke::Tick(float Delta)
{
	Super::Tick(Delta);
	// 时间基准的兜底放在 ApplyLook 里，这里不重复初始化，避免两处逻辑走偏。
	ApplyLook();
}

// 剩余寿命：只用本地时间轴。
// 上一版拿本地时钟去减服务器写的 ExpiresAt，两个时钟不同源 —— 本地时钟一超前，
// 剩余寿命就是负数，Alpha 被夹到 0，烟从生成那一刻起完全看不见。
float AExtractionSmoke::RemainingSeconds(float Age) const
{
	return LifetimeSeconds - Age;
}

void AExtractionSmoke::ApplyLook()
{
	if (!Cloud) return;
	const UWorld* World = GetWorld();
	if (!World) return;
	// 必须在这里兜住未初始化的情况：Refresh 会在 BeginPlay 里先于第一个 Tick 调用，
	// 那时 BornAt 还是 -1，Age 会被算成 Now+1 —— 服务器跑了几十秒后就是几百秒，
	// 直接超过寿命让 Alpha 归零，烟雾从生成起就完全看不见。
	if (BornAt < 0.f) BornAt = World->GetTimeSeconds();
	const float Age = FMath::Max(static_cast<float>(World->GetTimeSeconds() - BornAt), 0.f);
	const float FadeOut = FMath::Min(FadeOutSeconds, LifetimeSeconds * .35f);
	// 鼓胀比淡入快：半径先到位，再提高浓度，避免相机周围先起雾而看不到烟团本身。
	const float Grow = FMath::Clamp(Age / FMath::Max(FadeInSeconds * .45f, .01f), 0.f, 1.f);
	const float Inflate = FMath::InterpEaseOut(.35f, 1.f, Grow, 2.f);
	// 不透明度从 0.7 起步，不再平方：出生那一帧就得是一团能看见的烟。
	// 旧写法从 0 起淡入，只要 Tick 没跑起来或者 Age 卡在 0，Alpha 就永远停在 0，
	// 表现就是"烟完全不出现"而不是"烟淡"。
	const float FadeIn = FMath::Clamp(Age / FMath::Max(FadeInSeconds, .01f), 0.f, 1.f);
	const float FadeInAlpha = FMath::Lerp(SmokeFadeInFloor, 1.f, FadeIn);
	const float FadeOutAlpha = FMath::Clamp(RemainingSeconds(Age) / FMath::Max(FadeOut, .01f), 0.f, 1.f);
	const float Alpha = FMath::Min(FadeInAlpha, FadeOutAlpha);
	// 整团按当前鼓胀比例缩放，垂直方向压缩成贴地烟团。基础球半径 50 厘米，所以除以 50。
	const float Scale = Radius / 50.f * Inflate;
	// 每层按偏移、尺寸、浓度摆放；整团的浓度再乘上全局 Alpha，淡入淡出对所有层一致。
	const bool bAlphaChanged = !FMath::IsNearlyEqual(Alpha, AppliedAlpha, .004f);
	AppliedAlpha = Alpha;
	if (bCloudFxActive)
	{
		// 云团系统没有对外暴露用户参数，尺寸只能用组件缩放控制，写世界缩放是为了
		// 不被父级（根球体）已经带上的 Radius/50 再放大一遍。
		// 消散没有 alpha 可写，用缩到 0 表示：视觉上就是一整团烟被吹散摊开。
		const float Dissipate = FMath::Clamp(Alpha, 0.f, 1.f);
		// 视觉半径 = 遮挡半径 × SizeScale；Z 再乘 Aspect（1 = 正球，>1 = 竖高烟柱）。
		const float VisualRadius = Radius * CloudSizeScale;
		const float BaseScale =
			VisualRadius / FMath::Max(CloudSystemRadius, 1.f) * Inflate * Dissipate;
		// 偏移按三个轴分别缩。Z 只在压扁时跟着缩（Aspect<1），拉高时不缩：
		// 垂直方向的高度由每层云自身的 Z 缩放撑起来，如果把层间距也乘 4，
		// 16 层会被拉成一条有空带的稀疏柱子，中间能看穿。
		const float VerticalSpread = VisualRadius * FMath::Min(CloudAspect, 1.f);
		const FVector Spread(VisualRadius, VisualRadius, VerticalSpread);
		const FVector Center = GetActorLocation();
		for (int32 Index = 0; Index < CloudFxLayers.Num(); ++Index)
		{
			UNiagaraComponent* Fx = CloudFxLayers[Index];
			if (!Fx || !Fx->IsVisible()) continue;
			const FSmokeCloudLayer& Layer = CloudLayerLayout[Index];
			const float LayerScale = BaseScale * Layer.Scale;
			Fx->SetWorldScale3D(FVector(LayerScale, LayerScale, LayerScale * CloudAspect));
			// 偏移按整团半径算，不用相对坐标：父级（根球体）带着 Radius/50 的缩放，
			// 写相对位置会被再放大一次，九层会散出复制半径之外。
			Fx->SetWorldLocation(Center + Layer.Offset * Spread * Inflate);
			// 每层一个固定朝向，客户端各算各的也能保持一致（不能用随机）。
			Fx->SetWorldRotation(FRotator(0.f, Layer.Yaw, 0.f));
		}
	}
	// 云团接管时球体已经隐藏，写尺寸和透明度纯属浪费，循环条件里直接跳过。
	for (int32 Index = 0; !bCloudFxActive && Index < PuffLayerCount; ++Index)
	{
		// 第 0 层用主团，其余按顺序对应 Puffs；两层列表长度不一致时跳过缺项。
		UStaticMeshComponent* Mesh = Index == 0 ? Cloud.Get()
			: Puffs.IsValidIndex(Index - 1) ? Puffs[Index - 1].Get() : nullptr;
		if (!Mesh) continue;
		const FSmokePuffLayer& Layer = PuffLayers[Index];
		// 主团就是根组件。根组件的位置一个字都不能碰：对无父级的组件来说
		// "相对位置"就是世界坐标，写 Layer.Offset * 50 会把烟幕搬到 Layer.Offset*50，
		// 写 ZeroVector 则是把整个 Actor 搬到 (0,0,0) —— 两种写法都是错的。
		// 根组件保持 spawn 位置不动，只有挂在它下面的子团才按层摆放。
		if (Index > 0) Mesh->SetRelativeLocation(Layer.Offset * 50.f);
		// 缩放：辅团的父级就是主团，父级缩放里已经含了 Radius/50 × Inflate，
		// 所以辅团只写层系数本身（不能再乘 Inflate，否则鼓胀期间辅团会比主团小一大截）。
		const float LayerScale = Index == 0 ? Scale : Layer.Scale;
		Mesh->SetRelativeScale3D(FVector(LayerScale, LayerScale, LayerScale * CloudAspect));
		if (bAlphaChanged)
		{
			if (auto* Dynamic = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)))
				Dynamic->SetScalarParameterValue(TEXT("Opacity"), Alpha * Layer.Alpha);
		}
	}
	// 诊断日志：每 0.5 秒一行，能直接看出烟在哪、多浓、多大。定位完可以删掉。
	if (LastLogAge < 0.f || Age - LastLogAge >= .5f)
	{
		LastLogAge = Age;
		const float FxScale = (bCloudFxActive && CloudFxLayers.Num() > 0 && CloudFxLayers[0])
			? CloudFxLayers[0]->GetComponentScale().X : 0.f;
		UE_LOG(LogTemp, Warning,
			TEXT("[Smoke] %s loc=%s age=%.2f alpha=%.2f inflate=%.2f scale=%.2f radius=%.0f life=%.1f visual=%s layers=%d fxScale=%.2f"),
			*GetName(), *GetActorLocation().ToCompactString(), Age, Alpha, Inflate, Scale, Radius, LifetimeSeconds,
			bCloudFxActive ? TEXT("niagara") : TEXT("spheres"),
			bCloudFxActive ? FMath::Clamp(CloudLayerCount, 1, CloudFxLayers.Num()) : 0,
			FxScale);
	}
	// 自检基准必须在这里兜底，理由和 BornAt 一样：Refresh 会先于 BeginPlay 调用
	// （复制回调、验收脚本都会走这条路），那时基准还是零向量，把正常的出生坐标
	// 误判成"根组件被写了世界坐标"，会盖掉真正的漂移告警。
	if (!bSpawnCaptured)
	{
		bSpawnCaptured = true;
		SpawnLocation = GetActorLocation();
	}
	// 自检：这个 Actor 不该移动。一旦有人又在根组件上写了位置，这里立刻喊一声。
	// 只在服务器判（客户端 BeginPlay 时位置可能还没同步下来，会误报）。
	if (!bDriftReported && HasAuthority())
	{
		const float Drift = static_cast<float>(FVector::Dist(GetActorLocation(), SpawnLocation));
		if (Drift > 50.f)
		{
			bDriftReported = true;
			UE_LOG(LogTemp, Error,
				TEXT("[Smoke] %s drifted %.0fcm from spawn %s to %s — something wrote a world position on the root component"),
				*GetName(), Drift, *SpawnLocation.ToCompactString(), *GetActorLocation().ToCompactString());
		}
	}
}

// 基础球半径为 50 厘米，因此用 Radius/50 缩放；这是复制半径的表现回调。
void AExtractionSmoke::Refresh()
{
	// 首次刷新时把每层材质各自提升为动态实例，否则无法按帧调整不透明度。
	TArray<UStaticMeshComponent*> Meshes;
	Meshes.Reserve(PuffLayerCount);
	if (Cloud) Meshes.Add(Cloud);
	for (const TObjectPtr<UStaticMeshComponent>& Puff : Puffs)
	{
		if (Puff) Meshes.Add(Puff);
	}
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		if (Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0))) continue;
		if (auto* Source = Mesh->GetMaterial(0))
			Mesh->SetMaterial(0, UMaterialInstanceDynamic::Create(Source, this));
	}
	// 客户端在没有本地时间轴时也要有可见尺寸，立即按满尺寸摆放一次。
	AppliedAlpha = -1.f;
	// 后加入的客户端可能错过 BeginPlay 时的那次判定，这里补一次（幂等）。
	ApplyVisualBackend();
	ApplyLook();
	// 这一行把"看不见"的四个必要条件一次列全：组件在、网格在、材质在、位置没被搬走。
	UE_LOG(LogTemp, Warning,
		TEXT("[Smoke] %s refresh meshes=%d authority=%d world=%.2f loc=%s mesh=%d mat=%d"),
		*GetName(), Meshes.Num(), HasAuthority() ? 1 : 0,
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
		*GetActorLocation().ToCompactString(),
		Cloud->GetStaticMesh() ? 1 : 0, Cloud->GetMaterial(0) ? 1 : 0);
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
	// 判定用和表现同一个垂直压缩：烟画成正球，遮挡范围也得是正球。
	const float Aspect = FMath::Clamp(GetDefault<UExtractionRules>()->SmokeCloudAspect, .2f, 6.f);
	const FVector Scale(1.f, 1.f, 1.f / Aspect);
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
	const float Aspect = FMath::Clamp(GetDefault<UExtractionRules>()->SmokeCloudAspect, .2f, 6.f);
	const FVector Scale(1.f, 1.f, 1.f / Aspect);
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
