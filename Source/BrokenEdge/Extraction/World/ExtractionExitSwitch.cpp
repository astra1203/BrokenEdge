#include "BrokenEdge/Extraction/World/ExtractionExitSwitch.h"
#include "BrokenEdge/Extraction/World/ExtractionExit.h"
#include "BrokenEdge/Extraction/World/ExtractionDrawbridge.h"
#include "BrokenEdge/Extraction/World/ExtractionWater.h"
#include "BrokenEdge/Extraction/Combat/ExtractionCombatComponent.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AExtractionExitSwitch::AExtractionExitSwitch()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(RootComponent);
	if (Cube.Succeeded()) Body->SetStaticMesh(Cube.Object);
	Body->SetRelativeLocation(FVector(0, 0, 70));
	Body->SetRelativeScale3D(FVector(.35f, .2f, 1.4f));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Lamp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lamp"));
	Lamp->SetupAttachment(RootComponent);
	if (Sphere.Succeeded()) Lamp->SetStaticMesh(Sphere.Object);
	Lamp->SetRelativeLocation(FVector(0, 0, 150));
	Lamp->SetRelativeScale3D(FVector(.18f));
	Lamp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Lamp->SetCastShadow(false);
}

void AExtractionExitSwitch::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionExitSwitch, bActive);
	DOREPLIFETIME(AExtractionExitSwitch, bPulled);
	DOREPLIFETIME(AExtractionExitSwitch, Exit);
	DOREPLIFETIME(AExtractionExitSwitch, Bridge);
	DOREPLIFETIME(AExtractionExitSwitch, Water);
}

void AExtractionExitSwitch::ResetForRound(bool bInActive)
{
	if (!HasAuthority()) return;
	bActive = bInActive;
	bPulled = false;
	OnRep_State();
	ForceNetUpdate();
}

bool AExtractionExitSwitch::CanReach(ABrokenEdgeCharacter* Character) const
{
	if (!bActive || bPulled || !IsValid(Character)) return false;
	const FVector Point = GetActorLocation() + FVector(0, 0, 100);
	if (FVector::DistSquared(Character->GetActorLocation(), Point) > FMath::Square(200.f)) return false;
	FVector Eye;
	FRotator Rotation;
	Character->GetActorEyesViewPoint(Eye, Rotation);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ExtractionExitSwitch), false, Character);
	if (Character->GetEquippedWeapon()) Params.AddIgnoredActor(Character->GetEquippedWeapon());
	Params.AddIgnoredActor(this);
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, Eye, Point, ECC_Visibility, Params);
}

FString AExtractionExitSwitch::Pull(ABrokenEdgeCharacter* Character)
{
	if (!HasAuthority()) return {};
	if (!Character || !Character->GetExtractionCombat()->CanAct()) return TEXT("当前无法操作");
	if (!CanReach(Character)) return TEXT("距离电闸太远");
	if (IsValid(Bridge))
	{
		Bridge->Lower();
		return Bridge->Label.ToString() + TEXT("正在放下");
	}
	if (IsValid(Water))
	{
		if (!Water->StartFill()) return TEXT("已经在注水");
		bPulled = true;
		OnRep_State();
		ForceNetUpdate();
		return Water->Label.ToString() + TEXT("开始注水");
	}
	if (!IsValid(Exit) || Exit->RoundStartedAt < 0) return TEXT("电闸尚未通电");
	bPulled = true;
	OnRep_State();
	ForceNetUpdate();
	Exit->SwitchPulled(GetWorld()->GetTimeSeconds());
	const int32 Pulled = Exit->PulledSwitches(), Required = Exit->Definition.SwitchesRequired;
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_SWITCH_PULLED Exit=%s %d/%d"), *Exit->Definition.Id.ToString(), Pulled,
	       Required);
	return Pulled >= Required
		       ? Exit->Definition.Label.ToString() + TEXT("已开放")
		       : FString::Printf(TEXT("电闸已拉下 %d/%d"), Pulled, Required);
}

FString AExtractionExitSwitch::PromptText() const
{
	if (IsValid(Bridge)) return TEXT("F  ") + Bridge->Label.ToString() + TEXT(" · 解锁放桥");
	if (IsValid(Water)) return TEXT("F  ") + Water->Label.ToString() + TEXT(" · 注水");
	const FString Name = IsValid(Exit) ? Exit->Definition.Label.ToString() + TEXT("电闸") : TEXT("电闸");
	const int32 Pulled = IsValid(Exit) ? Exit->PulledSwitches() : 0;
	const int32 Required = IsValid(Exit) ? Exit->Definition.SwitchesRequired : 0;
	return FString::Printf(TEXT("F  %s · 拉下（%d/%d）"), *Name, Pulled, Required);
}

void AExtractionExitSwitch::OnRep_State()
{
	if (!LampMaterial) LampMaterial = Lamp->CreateDynamicMaterialInstance(0);
	if (!LampMaterial) return;
	const FLinearColor Color = !bActive ? FLinearColor(.05f, .05f, .05f) : bPulled
		                                                                   ? FLinearColor(.1f, 1, .3f)
		                                                                   : FLinearColor(1, .1f, .05f);
	LampMaterial->SetVectorParameterValue(TEXT("Color"), Color);
}
