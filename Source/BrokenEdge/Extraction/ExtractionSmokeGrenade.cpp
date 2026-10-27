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
	// 未落地就坠出世界的兜底：给保险丝一个上界，避免弹体永久留在地图外。
	if (HasAuthority()) SetLifeSpan(FMath::Max(FuseAt, 0.0) + 30.0);
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
	bSettled = true;
	Flight->StopMovementImmediately();
	Flight->SetComponentTickEnabled(false);
	SetActorLocation(Location + FVector(0, 0, 4));
	const float Fuse = FuseSeconds;
	FuseAt = GetWorld()->GetTimeSeconds() + Fuse;
	ForceNetUpdate();
	GetWorld()->GetTimerManager().SetTimer(FuseTimer, this, &AExtractionSmokeGrenade::Ignite, Fuse, false);
}

void AExtractionSmokeGrenade::Ignite()
{
	if (!HasAuthority()) return;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (auto* Smoke = GetWorld()->SpawnActor<AExtractionSmoke>(GetActorLocation(), FRotator::ZeroRotator, Params))
		Smoke->SetTuning(Radius, Seconds);
	// 烟幕已经生成，弹体不再有意义；销毁后客户端靠烟幕自身的复制接管表现。
	Destroy();
}
