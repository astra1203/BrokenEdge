#include "ExtractionSmokeGrenade.h"
#include "ExtractionSmoke.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
// 烟雾弹投掷物：服务器用抛物线把烟雾送到落点，静止后等待 FuseSeconds 再生成烟幕。
// 飞行与保险丝计时只在服务器推进，客户端依靠复制的位置和 ExpiresAt 判断是否已起烟。
// 起烟后自毁，因此投掷物本身不进 Blocks 查询，也不提供任何遮挡。

AExtractionSmokeGrenade::AExtractionSmokeGrenade()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	Shell = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shell"));
	SetRootComponent(Shell);
	// 只对世界静态几何产生阻挡，忽略角色和投射物，避免被自己或队友弹开。
	Shell->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Shell->SetCollisionResponseToAllChannels(ECR_Ignore);
	Shell->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Shell->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Shell->SetCollisionObjectType(ECC_WorldDynamic);
	Shell->SetCanEverAffectNavigation(false);
	Shell->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
	if (Sphere.Succeeded()) Shell->SetStaticMesh(Sphere.Object);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Game/Extraction/Abilities/M_ExtractionSmokeShell"));
	if (Material.Succeeded()) Shell->SetMaterial(0, Material.Object);
	// 小弹体：基础球半径 50 厘米，缩到约 5 厘米半径。
	Shell->SetRelativeScale3D(FVector(.1f));
	Flight = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Flight"));
	Flight->UpdatedComponent = Shell;
	Flight->InitialSpeed = 1800.f;
	Flight->MaxSpeed = 2600.f;
	// 重力略高于真实值，让投掷弧线更紧凑、落点更好预判。
	Flight->ProjectileGravityScale = 1.35f;
	Flight->bShouldBounce = false;
	Flight->bRotationFollowsVelocity = false;
}

void AExtractionSmokeGrenade::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority()) return;
	// 未落地就坠出世界的兜底：给保险丝一个上界，避免弹体永久留在地图外。
	SetLifeSpan(FuseAt + 30.0);
	// 飞行超时兜底：投掷物可能一直撞不到任何几何（飞出地图边缘、卡进缝隙、
	// 或者碰撞通道被忽略），那样永远不会有 NotifyHit，也就永远不起烟。
	// 玩家看到的是"扔出去什么都没有"，比在落点起烟难查得多，所以这里强制点燃。
	GetWorld()->GetTimerManager().SetTimer(
		FuseTimer, this, &AExtractionSmokeGrenade::Ignite, MaxAirborneSeconds, false);
}

void AExtractionSmokeGrenade::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionSmokeGrenade, FuseAt);
}

void AExtractionSmokeGrenade::Launch(const FVector& From, const FVector& To, float Speed)
{
	SetActorLocation(From);
	Flight->Velocity = (To - From).GetSafeNormal() * Speed + FVector(0, 0, 260);
	// 飞行阶段不许被过早销毁；真正的寿命由 Ignite 决定。
	SetLifeSpan(0);
}

void AExtractionSmokeGrenade::NotifyHit(UPrimitiveComponent* Component, AActor* Other, UPrimitiveComponent* OtherComponent,
                                        bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse,
                                        const FHitResult& Hit)
{
	Super::NotifyHit(Component, Other, OtherComponent, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
	if (!HasAuthority() || bSettled) return;
	Settle(HitLocation);
}

void AExtractionSmokeGrenade::Settle(const FVector& Location)
{
	if (bSettled || bIgnited) return;
	bSettled = true;
	Flight->StopMovementImmediately();
	Flight->SetComponentTickEnabled(false);
	SetActorLocation(Location + FVector(0, 0, 4));
	const float Fuse = FuseSeconds;
	FuseAt = GetWorld()->GetTimeSeconds() + Fuse;
	ForceNetUpdate();
	// 复用同一个定时器句柄：这一步同时取消了 BeginPlay 里的飞行超时。
	GetWorld()->GetTimerManager().SetTimer(FuseTimer, this, &AExtractionSmokeGrenade::Ignite, Fuse, false);
	UE_LOG(LogTemp, Warning, TEXT("[SmokeGrenade] settle at %s fuse=%.2f"),
		*GetActorLocation().ToCompactString(), Fuse);
}

void AExtractionSmokeGrenade::Ignite()
{
	if (!HasAuthority() || bIgnited) return;
	bIgnited = true;
	GetWorld()->GetTimerManager().ClearTimer(FuseTimer);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Smoke = GetWorld()->SpawnActor<AExtractionSmoke>(GetActorLocation(), FRotator::ZeroRotator, Params);
	if (!Smoke)
	{
		UE_LOG(LogTemp, Error, TEXT("[SmokeGrenade] smoke spawn failed at %s"),
			*GetActorLocation().ToCompactString());
		return;
	}
	Smoke->SetTuning(Radius, Seconds);
	UE_LOG(LogTemp, Warning, TEXT("[SmokeGrenade] ignite at %s radius=%.0f seconds=%.1f settled=%d"),
		*GetActorLocation().ToCompactString(), Radius, Seconds, bSettled ? 1 : 0);
	// 烟幕已经生成，弹体不再有意义；销毁后客户端靠烟幕自身的复制接管表现。
	Destroy();
}
