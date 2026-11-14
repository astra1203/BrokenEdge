#include "BrokenEdge/Extraction/World/ExtractionDoor.h"
#include "BrokenEdge/Extraction/Core/ExtractionInteractionComponent.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NavigationSystem.h"

AExtractionDoor::AExtractionDoor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Door = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Door"));
	Door->SetupAttachment(RootComponent);
	Door->SetMobility(EComponentMobility::Movable);
	Door->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Door->SetCanEverAffectNavigation(true);
}

void AExtractionDoor::BeginPlay()
{
	Super::BeginPlay();
	ClosedRotation = Door->GetRelativeRotation();
	SetActorTickEnabled(true);
	ApplyMotion();
}

void AExtractionDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExtractionDoor, bOpen);
	DOREPLIFETIME(AExtractionDoor, MotionStartedAt);
}

double AExtractionDoor::Now() const
{
	const auto* GS = GetWorld()->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

FVector AExtractionDoor::InteractionPoint() const { return Door->Bounds.Origin; }

bool AExtractionDoor::CanReach(const ABrokenEdgeCharacter* C) const
{
	if (!IsValid(C) || C->GetWorld() != GetWorld() || FVector::DistSquared(C->GetActorLocation(), InteractionPoint()) >
		FMath::Square(220.f)) return false;
	FVector Eye;
	FRotator Rotation;
	C->GetActorEyesViewPoint(Eye, Rotation);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OrdinaryDoor), false, C);
	return !GetWorld()->LineTraceSingleByChannel(Hit, Eye, InteractionPoint(), ECC_Visibility, Params) || Hit.GetActor()
		== this;
}

bool AExtractionDoor::SweepOccupied() const
{
	if (!Door->GetStaticMesh()) return true;
	const auto Bounds = Door->GetStaticMesh()->GetBoundingBox();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DoorSweep), false, this);
	const FCollisionObjectQueryParams Objects(ECC_Pawn);
	// Test the whole swing, not just the closed doorway; repeat while moving to catch entering pawns.
	for (int32 Step = 0; Step <= 16; ++Step)
	{
		FRotator R = ClosedRotation;
		R.Yaw += OpenAngle * Step / 16.f;
		const FTransform Local(R, Door->GetRelativeLocation(), Door->GetRelativeScale3D());
		const FTransform T = Local * GetActorTransform();
		const FVector Extent = Bounds.GetExtent() * T.GetScale3D().GetAbs() + FVector(8);
		if (GetWorld()->OverlapAnyTestByObjectType(T.TransformPosition(Bounds.GetCenter()), T.GetRotation(), Objects,
		                                           FCollisionShape::MakeBox(Extent), Params))
			return true;
	}
	return false;
}

FString AExtractionDoor::Toggle(ABrokenEdgeCharacter* C)
{
	if (!HasAuthority() || !CanReach(C) || !C->GetExtractionInteraction() || !C->GetExtractionInteraction()->CanAct())
		return TEXT("无法操作这扇门");
	if (MotionStartedAt >= 0 && Now() - MotionStartedAt < MoveSeconds) return TEXT("门正在移动");
	if (SweepOccupied()) return TEXT("门边有人，请让开");
	bOpen = !bOpen;
	MotionStartedAt = Now();
	ForceNetUpdate();
	OnRep_Motion();
	return {};
}

void AExtractionDoor::OnRep_Motion()
{
	if (!HasActorBegunPlay()) return;
	if (MotionStartedAt >= 0 && MotionStartedAt != LastSoundAt && Now() - MotionStartedAt < MoveSeconds)
	{
		LastSoundAt = MotionStartedAt;
		if (GetNetMode() != NM_DedicatedServer) UGameplayStatics::PlaySoundAtLocation(
			this, bOpen ? OpenSound : CloseSound, InteractionPoint());
	}
	SetActorTickEnabled(true);
	ApplyMotion();
}

void AExtractionDoor::ApplyMotion()
{
	const float Alpha = MotionStartedAt < 0
		                    ? 1.f
		                    : FMath::Clamp(float((Now() - MotionStartedAt) / FMath::Max(.1f, MoveSeconds)), 0.f, 1.f);
	FRotator Rotation = ClosedRotation;
	Rotation.Yaw += OpenAngle * (bOpen ? Alpha : 1.f - Alpha);
	Door->SetRelativeRotation(Rotation);
	if (Alpha >= 1.f)
	{
		SetActorTickEnabled(false);
		if (HasAuthority()) FNavigationSystem::UpdateComponentData(*Door);
	}
}

void AExtractionDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && !bOpen && SweepOccupied())
	{
		// Reverse continuously if someone enters during closing; preserve the current angle.
		const double Elapsed = FMath::Clamp(Now() - MotionStartedAt, 0., double(MoveSeconds));
		bOpen = true;
		MotionStartedAt = Now() - (MoveSeconds - Elapsed);
		ForceNetUpdate();
	}
	ApplyMotion();
}
