#include "ExtractionExit.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
// 撤离点 Actor 保存区域、开放时段和已使用名额，并将这些状态复制给客户端。
// 这里只提供可用性与占额操作；玩家停留计时和物资结算由 ExtractionGameMode 负责。

// 创建无碰撞的区域标记与文字；AlwaysRelevant 让远处客户端也可获得撤离点状态。
AExtractionExit::AExtractionExit()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
	Marker->SetupAttachment(RootComponent);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cube"));
	if (Mesh.Succeeded()) Marker->SetStaticMesh(Mesh.Object);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Game/Extraction/Materials/M_ExtractionExit"));
	if (Material.Succeeded()) Marker->SetMaterial(0, Material.Object);
	Marker->SetRelativeScale3D(FVector(5, 5, .03));
	Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marker->SetCastShadow(false);
	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(RootComponent);
	Label->SetAbsolute(false, false, true);
	Label->SetRelativeLocation(FVector(0, 0, 100));
	Label->SetRelativeRotation(FRotator(0, 90, 0));
	Label->SetText(FText::FromString(TEXT("EXTRACTION\n10 s")));
	Label->SetWorldSize(45);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetTextRenderColor(FColor(70, 230, 170));
}

// 把世界位置变换到撤离点局部坐标，再减去 BoundsOffset 检测半尺寸；不是依靠碰撞重叠事件。
bool AExtractionExit::Contains(const FVector& Position) const
{
	const FVector D = GetActorTransform().InverseTransformPosition(Position) - Definition.BoundsOffset;
	return FMath::Abs(D.X) <= Definition.HalfExtent.X && FMath::Abs(D.Y) <= Definition.HalfExtent.Y
		&& FMath::Abs(D.Z) <= Definition.HalfExtent.Z;
}

// 注册定义、开局时间和使用名额的复制；声明 Replicated 还需要在此加入复制列表。
void AExtractionExit::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionExit, Definition);
	DOREPLIFETIME(AExtractionExit, RoundStartedAt);
	DOREPLIFETIME(AExtractionExit, UsedCapacity);
}

// 仅服务器接受有效定义，设置 Actor 变换并主动更新本地表现；客户端通过 OnRep 更新。
void AExtractionExit::Configure(const FExtractionExitDefinition& InDefinition)
{
	if (!HasAuthority() || !InDefinition.IsValid()) return;
	Definition = InDefinition;
	SetActorTransform(Definition.Transform);
	OnRep_Definition();
	ForceNetUpdate();
}

// 根据收到的定义更新地面标记大小和停留秒数文字，不在此推进撤离逻辑。
void AExtractionExit::OnRep_Definition()
{
	Marker->SetRelativeScale3D(FVector(Definition.HalfExtent.X / 50, Definition.HalfExtent.Y / 50, .03));
	Label->SetText(FText::Format(NSLOCTEXT("Extraction", "ExitLabel", "{0}\n{1} s"),
		Definition.Label, FText::AsNumber(Definition.HoldSeconds)));
}

// 服务器记录开局时间并清零名额；开放与关闭时间均相对该时刻计算。
void AExtractionExit::StartRound(double ServerTime)
{
	if (!HasAuthority()) return;
	RoundStartedAt = ServerTime;
	UsedCapacity = 0;
	ForceNetUpdate();
}

// 依次检查定义、名额、开放和关闭时刻；空字符串表示当前可用，关闭边界包含截止时刻。
FString AExtractionExit::UnavailableReason(double ServerTime) const
{
	if (!Definition.IsValid() || RoundStartedAt < 0) return TEXT("尚未开放");
	if (Definition.Capacity > 0 && UsedCapacity >= Definition.Capacity) return TEXT("名额已用尽");
	const double Elapsed = ServerTime - RoundStartedAt;
	if (Elapsed < Definition.OpensAfterSeconds)
		return FString::Printf(TEXT("%.0f 秒后开放"), FMath::CeilToDouble(Definition.OpensAfterSeconds - Elapsed));
	if (Definition.ClosesAfterSeconds > 0 && Elapsed >= Definition.ClosesAfterSeconds) return TEXT("已关闭");
	return {};
}

// 复用不可用原因作为单一判断依据，避免 UI 提示与服务器条件分叉。
bool AExtractionExit::Available(double ServerTime) const { return UnavailableReason(ServerTime).IsEmpty(); }

// 服务器再次验证当前可用性后增加已用名额；Capacity 为零表示无限制，并非不统计人数。
bool AExtractionExit::Consume()
{
	if (!HasAuthority() || !Available(GetWorld()->GetTimeSeconds())) return false;
	++UsedCapacity;
	ForceNetUpdate();
	return true;
}
