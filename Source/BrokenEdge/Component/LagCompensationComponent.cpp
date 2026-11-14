// Fill out your copyright notice in the Description page of Project Settings.


#include "LagCompensationComponent.h"

#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Extraction/Core/ExtractionState.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

static TAutoConsoleVariable<int32> CVarMaxRewindMs(
	TEXT("BrokenEdge.Net.MaxRewindMs"), 200,
	TEXT("Upper bound of server-side lag compensation in milliseconds; 0 disables rewinding."));

static TAutoConsoleVariable<int32> CVarDebugRewind(
	TEXT("BrokenEdge.Net.DebugRewind"), 0,
	TEXT("Draw the rewound hit bodies (green) and the latest recorded pose (red) whenever a rewound trace hits."));

namespace
{
	/** 世界内全部延迟补偿组件；数量等于角色数，线性遍历即可。 */
	TArray<TWeakObjectPtr<ULagCompensationComponent>> GRewindComponents;

	FTransform ShapeWorld(const FHitShape& Shape, const FTransform& Bone) { return Shape.Local * Bone; }

	void DrawPose(const UWorld* World, const TArray<FHitShape>& Shapes, const TArray<FTransform>& Pose,
	              const FColor& Color)
	{
		for (const FHitShape& Shape : Shapes)
		{
			const FTransform W = ShapeWorld(Shape, Pose[Shape.Body]);
			const float Scale = W.GetMaximumAxisScale();
			switch (Shape.Kind)
			{
			case FHitShape::EKind::Sphere:
				DrawDebugSphere(World, W.GetLocation(), Shape.Radius * Scale, 12, Color, false, 5);
				break;
			case FHitShape::EKind::Capsule:
				DrawDebugCapsule(World, W.GetLocation(), (Shape.HalfLength + Shape.Radius) * Scale,
				                 Shape.Radius * Scale, W.GetRotation(), Color, false, 5);
				break;
			case FHitShape::EKind::Box:
				DrawDebugBox(World, W.GetLocation(), Shape.HalfExtent * Scale, W.GetRotation(), Color, false, 5);
				break;
			}
		}
	}
}

bool HitRewind::SegmentSphere(const FVector& Start, const FVector& End, const FVector& Center, float Radius,
                              float& OutT)
{
	const FVector D = End - Start;
	const FVector M = Start - Center;
	const double C = M.SizeSquared() - FMath::Square(Radius);
	if (C <= 0)
	{
		OutT = 0;
		return true;
	}
	const double A = D.SizeSquared();
	if (A < UE_SMALL_NUMBER) return false;
	const double B = FVector::DotProduct(M, D);
	const double Disc = B * B - A * C;
	if (Disc < 0) return false;
	const double T = (-B - FMath::Sqrt(Disc)) / A;
	if (T < 0 || T > 1) return false;
	OutT = T;
	return true;
}

bool HitRewind::SegmentCapsule(const FVector& Start, const FVector& End, float Radius, float HalfLength, float& OutT)
{
	// 起点已在胶囊内：到轴线段的距离不超过半径。
	if (FVector::DistSquared(Start, FVector(0, 0, FMath::Clamp(Start.Z, -HalfLength, HalfLength))) <= FMath::Square(
		Radius))
	{
		OutT = 0;
		return true;
	}
	float Best = 2;
	const FVector D = End - Start;
	const double A = D.X * D.X + D.Y * D.Y;
	if (A > UE_SMALL_NUMBER)
	{
		const double B = Start.X * D.X + Start.Y * D.Y;
		const double C = Start.X * Start.X + Start.Y * Start.Y - FMath::Square(Radius);
		const double Disc = B * B - A * C;
		if (Disc >= 0)
		{
			const double T = (-B - FMath::Sqrt(Disc)) / A;
			if (T >= 0 && T <= 1 && FMath::Abs(Start.Z + T * D.Z) <= HalfLength) Best = T;
		}
	}
	float T;
	if (SegmentSphere(Start, End, FVector(0, 0, HalfLength), Radius, T)) Best = FMath::Min(Best, T);
	if (SegmentSphere(Start, End, FVector(0, 0, -HalfLength), Radius, T)) Best = FMath::Min(Best, T);
	if (Best > 1) return false;
	OutT = Best;
	return true;
}

bool HitRewind::SegmentBox(const FVector& Start, const FVector& End, const FVector& HalfExtent, float& OutT)
{
	const FVector D = End - Start;
	double Min = 0, Max = 1;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (FMath::Abs(D[Axis]) < UE_SMALL_NUMBER)
		{
			if (FMath::Abs(Start[Axis]) > HalfExtent[Axis]) return false;
			continue;
		}
		double T1 = (-HalfExtent[Axis] - Start[Axis]) / D[Axis];
		double T2 = (HalfExtent[Axis] - Start[Axis]) / D[Axis];
		if (T1 > T2) Swap(T1, T2);
		Min = FMath::Max(Min, T1);
		Max = FMath::Min(Max, T2);
		if (Min > Max) return false;
	}
	OutT = Min;
	return true;
}

bool HitRewind::SegmentShape(const FHitShape& Shape, const FTransform& Bone, const FVector& Start, const FVector& End,
                             float Inflate, float& OutT)
{
	const FTransform W = ShapeWorld(Shape, Bone);
	const float Scale = FMath::Max(W.GetMaximumAxisScale(), UE_KINDA_SMALL_NUMBER);
	const float Pad = Inflate / Scale;
	// 仿射变换保持线段参数 t 不变，所以局部空间求得的 t 就是世界线段上的比例。
	const FVector LocalStart = W.InverseTransformPosition(Start);
	const FVector LocalEnd = W.InverseTransformPosition(End);
	switch (Shape.Kind)
	{
	case FHitShape::EKind::Sphere:
		return SegmentSphere(LocalStart, LocalEnd, FVector::ZeroVector, Shape.Radius + Pad, OutT);
	case FHitShape::EKind::Capsule:
		return SegmentCapsule(LocalStart, LocalEnd, Shape.Radius + Pad, Shape.HalfLength, OutT);
	case FHitShape::EKind::Box:
		return SegmentBox(LocalStart, LocalEnd, Shape.HalfExtent + FVector(Pad), OutT);
	}
	return false;
}

double HitRewind::MaxRewindSeconds()
{
	return FMath::Clamp(CVarMaxRewindMs.GetValueOnGameThread(), 0, 1000) / 1000.;
}

ULagCompensationComponent::ULagCompensationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 动画和移动都完成后再记录，保证记下的是本帧最终姿态。
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void ULagCompensationComponent::BeginPlay()
{
	Super::BeginPlay();
	Character = Character == nullptr ? Cast<ABrokenEdgeCharacter>(GetOwner()) : Character;
	// 只有服务器结算命中，客户端不需要记录历史。
	if (GetNetMode() == NM_Client)
	{
		SetComponentTickEnabled(false);
		return;
	}
	GRewindComponents.Add(this);
}

void ULagCompensationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GRewindComponents.RemoveAllSwap([this](const TWeakObjectPtr<ULagCompensationComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == this;
	});
	History.Reset();
	Super::EndPlay(EndPlayReason);
}

void ULagCompensationComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                              FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	SaveFrame();
}

bool ULagCompensationComponent::EnsureShapes()
{
	Character = Character == nullptr ? Cast<ABrokenEdgeCharacter>(GetOwner()) : Character;
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	UPhysicsAsset* PhysicsAsset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	if (!PhysicsAsset)
	{
		Shapes.Reset();
		History.Reset();
		return false;
	}
	if (PhysicsAsset == ShapeSource.Get() && Mesh->GetSkinnedAsset() == ShapeMesh.Get() && Shapes.Num()) return true;

	ShapeSource = PhysicsAsset;
	ShapeMesh = Mesh->GetSkinnedAsset();
	Bones.Reset();
	BoneIndices.Reset();
	Shapes.Reset();
	History.Reset();
	ShapeReach = 0;
	for (const USkeletalBodySetup* Body : PhysicsAsset->SkeletalBodySetups)
	{
		if (!Body) continue;
		const int32 BoneIndex = Mesh->GetBoneIndex(Body->BoneName);
		if (BoneIndex == INDEX_NONE) continue;
		const int32 Index = Bones.Add(Body->BoneName);
		BoneIndices.Add(BoneIndex);
		const FKAggregateGeom& Geometry = Body->AggGeom;
		auto Add = [&](FHitShape::EKind Kind, const FTransform& Local, float Radius, float HalfLength,
		               const FVector& HalfExtent)
		{
			FHitShape& Shape = Shapes.AddDefaulted_GetRef();
			Shape.Kind = Kind;
			Shape.Body = Index;
			Shape.Local = Local;
			Shape.Radius = Radius;
			Shape.HalfLength = HalfLength;
			Shape.HalfExtent = HalfExtent;
			ShapeReach = FMath::Max(ShapeReach, Local.GetLocation().Size() + HalfLength + Radius + HalfExtent.Size());
		};
		for (const FKSphereElem& Elem : Geometry.SphereElems)
			Add(FHitShape::EKind::Sphere, Elem.GetTransform(), Elem.Radius, 0, FVector::ZeroVector);
		for (const FKSphylElem& Elem : Geometry.SphylElems)
			Add(FHitShape::EKind::Capsule, Elem.GetTransform(), Elem.Radius, Elem.Length * .5f, FVector::ZeroVector);
		// 锥形胶囊按较大半径近似，命中体稍大但不会漏判。
		for (const FKTaperedCapsuleElem& Elem : Geometry.TaperedCapsuleElems)
			Add(FHitShape::EKind::Capsule, Elem.GetTransform(), FMath::Max(Elem.Radius0, Elem.Radius1),
			    Elem.Length * .5f, FVector::ZeroVector);
		for (const FKBoxElem& Elem : Geometry.BoxElems)
			Add(FHitShape::EKind::Box, Elem.GetTransform(), 0, 0, FVector(Elem.X, Elem.Y, Elem.Z) * .5f);
		// 凸包按其包围盒近似。
		for (const FKConvexElem& Elem : Geometry.ConvexElems)
			Add(FHitShape::EKind::Box, FTransform(Elem.ElemBox.GetCenter()) * Elem.GetTransform(), 0, 0,
			    Elem.ElemBox.GetExtent());
	}
	return Shapes.Num() > 0;
}

void ULagCompensationComponent::SaveFrame()
{
	Character = Character == nullptr ? Cast<ABrokenEdgeCharacter>(GetOwner()) : Character;
	if (Character == nullptr || !Character->HasAuthority() || !EnsureShapes()) return;
	const USkeletalMeshComponent* Mesh = Character->GetMesh();
	const double Now = GetWorld()->GetTimeSeconds();
	if (History.Num() && History.Last().Time >= Now) History.Pop(EAllowShrinking::No);

	FHitFrame& Frame = History.AddDefaulted_GetRef();
	Frame.Time = Now;
	Frame.Bones.SetNum(BoneIndices.Num());
	FBox Box(ForceInit);
	for (int32 Index = 0; Index < BoneIndices.Num(); ++Index)
	{
		Frame.Bones[Index] = Mesh->GetBoneTransform(BoneIndices[Index]);
		Box += Frame.Bones[Index].GetLocation();
	}
	Frame.Bounds = FSphere(Box.GetCenter(), Box.GetExtent().Size() + ShapeReach * Mesh->GetComponentScale().GetMax());

	// 保留刚好早于窗口的一帧，窗口起点仍可插值。
	int32 Expired = 0;
	const double Keep = FMath::Max<double>(MaxRecordTime, HitRewind::MaxRewindSeconds() + .1);
	while (Expired + 1 < History.Num() - 1 && Now - History[Expired + 1].Time > Keep) ++Expired;
	if (Expired) History.RemoveAt(0, Expired, EAllowShrinking::No);
}

bool ULagCompensationComponent::PoseAt(double Time, TArray<FTransform>& OutBones, FSphere& OutBounds) const
{
	if (History.Num() == 0) return false;
	const double Now = GetWorld()->GetTimeSeconds();
	Time = FMath::Clamp(Time, Now - HitRewind::MaxRewindSeconds(), Now);
	if (Time >= History.Last().Time || History.Num() == 1)
	{
		OutBones = History.Last().Bones;
		OutBounds = History.Last().Bounds;
		return true;
	}
	if (Time <= History[0].Time)
	{
		OutBones = History[0].Bones;
		OutBounds = History[0].Bounds;
		return true;
	}
	int32 Younger = History.Num() - 1;
	while (Younger > 0 && History[Younger - 1].Time > Time) --Younger;
	const FHitFrame& Old = History[Younger - 1];
	const FHitFrame& New = History[Younger];
	const float Alpha = static_cast<float>(
		FMath::Clamp((Time - Old.Time) / FMath::Max(New.Time - Old.Time, UE_SMALL_NUMBER), 0., 1.));
	OutBones.SetNum(Old.Bones.Num());
	for (int32 Index = 0; Index < Old.Bones.Num(); ++Index) OutBones[Index].Blend(Old.Bones[Index], New.Bones[Index],
	                                                                              Alpha);
	OutBounds = FSphere(FMath::Lerp(Old.Bounds.Center, New.Bounds.Center, Alpha), FMath::Max(Old.Bounds.W, New.Bounds.W));
	return true;
}

bool ULagCompensationComponent::CanBeRewound() const
{
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	return Mesh && History.Num() && Shapes.Num() && Mesh->IsQueryCollisionEnabled()
		&& Mesh->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block;
}

bool ULagCompensationComponent::TraceAt(double Time, const FVector& Start, const FVector& End, float Inflate,
                                        FRewindHit& OutHit) const
{
	TArray<FTransform> Pose;
	FSphere Bounds;
	float T;
	if (!PoseAt(Time, Pose, Bounds) || !HitRewind::SegmentSphere(Start, End, Bounds.Center, Bounds.W + Inflate, T))
		return false;
	float Best = 2;
	int32 BestBody = INDEX_NONE;
	for (const FHitShape& Shape : Shapes)
		if (HitRewind::SegmentShape(Shape, Pose[Shape.Body], Start, End, Inflate, T) && T < Best)
		{
			Best = T;
			BestBody = Shape.Body;
		}
	if (BestBody == INDEX_NONE) return false;
	OutHit.Character = Character;
	OutHit.Bone = Bones[BestBody];
	OutHit.Fraction = Best;
	OutHit.Location = FMath::Lerp(Start, End, Best);
	if (CVarDebugRewind.GetValueOnGameThread())
	{
		DrawPose(GetWorld(), Shapes, Pose, FColor::Green);
		DrawPose(GetWorld(), Shapes, History.Last().Bones, FColor::Red);
		DrawDebugLine(GetWorld(), Start, OutHit.Location, FColor::Yellow, false, 5);
	}
	return true;
}

bool ULagCompensationComponent::TraceWorldAt(const UWorld* World, double Time, const FVector& Start,
                                             const FVector& End, float Inflate, const TArray<const AActor*>& Ignore,
                                             FRewindHit& OutHit)
{
	bool bHit = false;
	for (const TWeakObjectPtr<ULagCompensationComponent>& Entry : GRewindComponents)
	{
		const ULagCompensationComponent* Component = Entry.Get();
		if (!Component || Component->GetWorld() != World || Ignore.Contains(Component->GetOwner())
			|| !Component->CanBeRewound())
			continue;
		FRewindHit Hit;
		if (Component->TraceAt(Time, Start, End, Inflate, Hit) && (!bHit || Hit.Fraction < OutHit.Fraction))
		{
			OutHit = Hit;
			bHit = true;
		}
	}
	return bHit;
}

void ULagCompensationComponent::GetRewindActors(const UWorld* World, TArray<AActor*>& OutActors)
{
	for (const TWeakObjectPtr<ULagCompensationComponent>& Entry : GRewindComponents)
		if (const ULagCompensationComponent* Component = Entry.Get(); Component && Component->GetWorld() == World
			&& Component->CanBeRewound())
			OutActors.Add(Component->GetOwner());
}

void ULagCompensationComponent::ApplyLegacyDamage(ABrokenEdgeCharacter* HitCharacter, float Damage)
{
	if (!Character || !HitCharacter || Damage <= 0) return;
	UGameplayStatics::ApplyDamage(HitCharacter, Damage, Character->Controller, Character->GetEquippedWeapon(),
	                              UDamageType::StaticClass());
}

FServerSideRewindResult ULagCompensationComponent::HitScanServerSideRewind(
	ABrokenEdgeCharacter* HitCharacter, const FVector_NetQuantize& TraceStart, const FVector_NetQuantize& HitLocation,
	float HitTime)
{
	FRewindHit Hit;
	const ULagCompensationComponent* Target = HitCharacter ? HitCharacter->GetLagCompensation().Get() : nullptr;
	const FVector TraceEnd = TraceStart + (HitLocation - TraceStart) * 1.25f;
	if (!Target || !Target->TraceAt(HitTime, TraceStart, TraceEnd, 0, Hit)) return FServerSideRewindResult();
	return FServerSideRewindResult{true, Hit.Bone == FName(TEXT("head"))};
}

FServerSideRewindResult ULagCompensationComponent::ProjectileServerSideRewind(
	ABrokenEdgeCharacter* HitCharacter, const FVector_NetQuantize& TraceStart,
	const FVector_NetQuantize100& InitialVelocity, float HitTime)
{
	const ULagCompensationComponent* Target = HitCharacter ? HitCharacter->GetLagCompensation().Get() : nullptr;
	if (!Target) return FServerSideRewindResult();
	FPredictProjectilePathParams PathParams;
	PathParams.bTraceWithCollision = false;
	PathParams.MaxSimTime = 4.f;
	PathParams.LaunchVelocity = InitialVelocity;
	PathParams.StartLocation = TraceStart;
	PathParams.SimFrequency = 15.f;
	FPredictProjectilePathResult PathResult;
	UGameplayStatics::PredictProjectilePath(this, PathParams, PathResult);
	for (int32 Index = 1; Index < PathResult.PathData.Num(); ++Index)
	{
		FRewindHit Hit;
		if (Target->TraceAt(HitTime, PathResult.PathData[Index - 1].Location, PathResult.PathData[Index].Location, 5.f,
		                    Hit))
			return FServerSideRewindResult{true, Hit.Bone == FName(TEXT("head"))};
	}
	return FServerSideRewindResult();
}

FShotgunServerSideRewindResult ULagCompensationComponent::ShotgunServerSideRewind(
	const TArray<ABrokenEdgeCharacter*>& HitCharacters, const FVector& TraceStart,
	const TArray<FVector_NetQuantize>& HitLocations, float HitTime)
{
	FShotgunServerSideRewindResult Result;
	for (const FVector_NetQuantize& HitLocation : HitLocations)
	{
		const FVector TraceEnd = TraceStart + (HitLocation - TraceStart) * 1.25f;
		FRewindHit Best;
		bool bHit = false;
		for (ABrokenEdgeCharacter* HitCharacter : HitCharacters)
		{
			const ULagCompensationComponent* Target = HitCharacter ? HitCharacter->GetLagCompensation().Get() : nullptr;
			FRewindHit Hit;
			if (Target && Target->TraceAt(HitTime, TraceStart, TraceEnd, 0, Hit) && (!bHit || Hit.Fraction < Best.
				Fraction))
			{
				Best = Hit;
				bHit = true;
			}
		}
		if (!bHit) continue;
		auto& Counts = Best.Bone == FName(TEXT("head")) ? Result.HeadShots : Result.BodyShots;
		++Counts.FindOrAdd(Best.Character);
	}
	return Result;
}

void ULagCompensationComponent::ProjectileServerScoreRequest_Implementation(ABrokenEdgeCharacter* HitCharacter,
                                                                            const FVector_NetQuantize& TraceStart,
                                                                            const FVector_NetQuantize100&
                                                                            InitialVelocity, float HitTime)
{
	if (GetWorld()->GetGameState<AExtractionGameState>()) return;
	const FServerSideRewindResult Confirm = ProjectileServerSideRewind(HitCharacter, TraceStart, InitialVelocity,
	                                                                   HitTime);
	if (Confirm.bHitConfirmed && Character && Character->GetEquippedWeapon())
		ApplyLegacyDamage(HitCharacter, Confirm.bHeadShot
			                                ? Character->GetEquippedWeapon()->GetHeadShotDamage()
			                                : Character->GetEquippedWeapon()->GetDamage());
}

void ULagCompensationComponent::HitScanServerScoreRequest_Implementation(ABrokenEdgeCharacter* HitCharacter,
                                                                         const FVector_NetQuantize& TraceStart,
                                                                         const FVector_NetQuantize& HitLocation,
                                                                         float HitTime)
{
	if (GetWorld()->GetGameState<AExtractionGameState>()) return;
	const FServerSideRewindResult Confirm = HitScanServerSideRewind(HitCharacter, TraceStart, HitLocation, HitTime);
	if (Confirm.bHitConfirmed && Character && Character->GetEquippedWeapon())
		ApplyLegacyDamage(HitCharacter, Confirm.bHeadShot
			                                ? Character->GetEquippedWeapon()->GetHeadShotDamage()
			                                : Character->GetEquippedWeapon()->GetDamage());
}

void ULagCompensationComponent::ShotgunServerScoreRequest_Implementation(
	const TArray<ABrokenEdgeCharacter*>& HitCharacters, const FVector& TraceStart,
	const TArray<FVector_NetQuantize>& HitLocations, float HitTime)
{
	if (GetWorld()->GetGameState<AExtractionGameState>()) return;
	const FShotgunServerSideRewindResult Confirm = ShotgunServerSideRewind(HitCharacters, TraceStart, HitLocations,
	                                                                        HitTime);
	if (!Character || !Character->GetEquippedWeapon()) return;
	for (ABrokenEdgeCharacter* HitCharacter : HitCharacters)
	{
		if (!HitCharacter) continue;
		const uint32* Head = Confirm.HeadShots.Find(HitCharacter);
		const uint32* Body = Confirm.BodyShots.Find(HitCharacter);
		ApplyLegacyDamage(HitCharacter, (Head ? *Head : 0) * Character->GetEquippedWeapon()->GetHeadShotDamage()
		                  + (Body ? *Body : 0) * Character->GetEquippedWeapon()->GetDamage());
	}
}
