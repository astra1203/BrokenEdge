// Fill out your copyright notice in the Description page of Project Settings.


#include "LagCompensationComponent.h"

#include "MovieSceneSequencePlayer.h"
#include "ScreenPass.h"
#include "BrokenEdge/BrokenEdge.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Weapon/Weapon.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"


// Sets default values for this component's properties
ULagCompensationComponent::ULagCompensationComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}


// Called when the game starts
void ULagCompensationComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...
}


// Called every frame
void ULagCompensationComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                              FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	SaveFramePackageServer();
}

void ULagCompensationComponent::SaveFramePackageServer()
{
	Character = Character == nullptr ? Cast<ABrokenEdgeCharacter>(GetOwner()) : Character;
	if (Character == nullptr || !Character->HasAuthority()) return;

	if (FrameHistory.Num() <= 1)
	{
		FFramePackage ThisFrame;
		SaveFramePackage(ThisFrame);
		FrameHistory.AddHead(ThisFrame);
	}
	else
	{
		double HistoryLength = FrameHistory.GetHead()->GetValue().Time - FrameHistory.GetTail()->GetValue().Time;
		while (HistoryLength > MaxRecordTime)
		{
			FrameHistory.RemoveNode(FrameHistory.GetTail());
			HistoryLength = FrameHistory.GetHead()->GetValue().Time - FrameHistory.GetTail()->GetValue().Time;
		}

		FFramePackage ThisFrame;
		SaveFramePackage(ThisFrame);
		FrameHistory.AddHead(ThisFrame);
	}
}

void ULagCompensationComponent::SaveFramePackage(FFramePackage& Package)
{
	if (Character == nullptr || !Character->HasAuthority()) return;

	Package.Time = GetWorld()->GetTimeSeconds();
	Package.Character = Character;

	for (auto& BoxPair : Character->HitCollisionBoxes)
	{
		FBoxInformation BoxInformation;
		BoxInformation.Location = BoxPair.Value->GetComponentLocation();
		BoxInformation.Rotation = BoxPair.Value->GetComponentRotation();
		BoxInformation.BoxExtent = BoxPair.Value->GetScaledBoxExtent();
		Package.HitBoxInfo.Add(BoxPair.Key, BoxInformation);
	}
}

void ULagCompensationComponent::ProjectileServerScoreRequest_Implementation(ABrokenEdgeCharacter* HitCharacter,
                                                                            const FVector_NetQuantize& TraceStart,
                                                                            const FVector_NetQuantize100&
                                                                            InitialVelocity, float HitTime)
{
	FServerSideRewindResult Confirm = ProjectileServerSideRewind(HitCharacter, TraceStart, InitialVelocity, HitTime);

	if (Character && HitCharacter && Confirm.bHitConfirmed && Character->GetEquippedWeapon())
	{
		const float Damage = Confirm.bHeadShot
			                     ? Character->GetEquippedWeapon()->GetHeadShotDamage()
			                     : Character->GetEquippedWeapon()->GetDamage();

		UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s 伤害：%.2f"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
		       *PrintNetMode(), Damage);

		UGameplayStatics::ApplyDamage(
			HitCharacter,
			Damage,
			Character->Controller,
			Character->GetEquippedWeapon(),
			UDamageType::StaticClass()
		);
	}
}

/**
 * @brief 执行服务器端倒带（投射物武器）
 * @param HitCharacter 被命中的角色
 * @param TraceStart 投射物起点
 * @param InitialVelocity 初始速度
 * @param HitTime 命中时间
 * @return 命中确认结果
 */
FServerSideRewindResult ULagCompensationComponent::ProjectileServerSideRewind(
	ABrokenEdgeCharacter* HitCharacter, const FVector_NetQuantize& TraceStart,
	const FVector_NetQuantize100& InitialVelocity, float HitTime)
{
	FFramePackage FrameToChrck = GetFrameToCheck(HitCharacter, HitTime);

	return ProjectileConfirmHit(FrameToChrck, HitCharacter, TraceStart, InitialVelocity, HitTime);
}

FFramePackage ULagCompensationComponent::GetFrameToCheck(ABrokenEdgeCharacter* HitCharacter, float HitTime)
{
	bool bReturn =
		HitCharacter == nullptr ||
		HitCharacter->GetLagCompensation() == nullptr ||
		HitCharacter->GetLagCompensation()->FrameHistory.GetHead() == nullptr ||
		HitCharacter->GetLagCompensation()->FrameHistory.GetTail() == nullptr;

	if (bReturn) return FFramePackage();

	FFramePackage FrameToCheck;

	bool bShouldInterpolate = true;

	// 得到命中角色的历史帧包
	const TDoubleLinkedList<FFramePackage>& History = HitCharacter->GetLagCompensation()->FrameHistory;

	const float OldestHistoryTime = History.GetTail()->GetValue().Time;
	const float NewestHistoryTime = History.GetHead()->GetValue().Time;

	if (OldestHistoryTime > HitTime)
	{
		return FFramePackage();
	}

	if (OldestHistoryTime == HitTime)
	{
		FrameToCheck = History.GetTail()->GetValue();
		bShouldInterpolate = false;
	}

	if (NewestHistoryTime <= HitTime)
	{
		FrameToCheck = History.GetHead()->GetValue();
		bShouldInterpolate = false;
	}

	TDoubleLinkedList<FFramePackage>::TDoubleLinkedListNode* Younger = History.GetHead();
	TDoubleLinkedList<FFramePackage>::TDoubleLinkedListNode* Older = Younger;
	while (Older->GetValue().Time > HitTime)
	{
		if (Older->GetNextNode() == nullptr)break;
		Older = Older->GetNextNode();

		if (Older->GetValue().Time > HitTime)Younger = Older;
	}
	if (Older->GetValue().Time == HitTime)
	{
		FrameToCheck = Older->GetValue();
		bShouldInterpolate = false;
	}
	if (bShouldInterpolate)
	{
		FrameToCheck = InterpBetweenFrames(Older->GetValue(), Younger->GetValue(), HitTime);
	}
	FrameToCheck.Character = HitCharacter;
	return FrameToCheck;
}

FFramePackage ULagCompensationComponent::InterpBetweenFrames(const FFramePackage& OlderFrame,
                                                             const FFramePackage& YoungerFrame, float HitTime)
{
	FFramePackage InterpFramePackage;
	InterpFramePackage.Time = HitTime;

	const float Distance = YoungerFrame.Time - OlderFrame.Time;
	const float InterpFraction = FMath::Clamp((HitTime - OlderFrame.Time) / Distance, 0.f, 1.f);

	for (auto& YoungerPair : YoungerFrame.HitBoxInfo)
	{
		const FName& BoxInfoName = YoungerPair.Key;

		const FBoxInformation& OlderBox = OlderFrame.HitBoxInfo[BoxInfoName];
		const FBoxInformation& YoungerBox = YoungerFrame.HitBoxInfo[BoxInfoName];

		FBoxInformation InterpBoxInfo;

		InterpBoxInfo.Location = FMath::VInterpTo(OlderBox.Location, YoungerBox.Location, 1.f, InterpFraction);
		InterpBoxInfo.Rotation = FMath::RInterpTo(OlderBox.Rotation, YoungerBox.Rotation, 1.f, InterpFraction);
		InterpBoxInfo.BoxExtent = YoungerBox.BoxExtent;

		InterpFramePackage.HitBoxInfo.Add(BoxInfoName, InterpBoxInfo);
	}


	return InterpFramePackage;
}


/**
 * @brief 确认命中（投射物武器）
 * @param Package 帧包
 * @param HitCharacter 被命中的角色
 * @param TraceStart 投射物起点
 * @param InitialVelocity 初始速度
 * @param HitTime 命中时间
 * @return 命中确认结果
 */
FServerSideRewindResult ULagCompensationComponent::ProjectileConfirmHit(const FFramePackage& FramePackage,
                                                                        ABrokenEdgeCharacter* HitCharacter,
                                                                        const FVector_NetQuantize& TraceStart,
                                                                        const FVector_NetQuantize100& InitialVelocity,
                                                                        float HitTime)
{
	FFramePackage CurrentFrame;
	CacheBoxPositions(HitCharacter, CurrentFrame);
	MoveBoxes(HitCharacter, FramePackage);
	EnableCharacterMeshCollision(HitCharacter, ECollisionEnabled::NoCollision);

	// 首先把头的命中框设置碰撞
	UBoxComponent* HeadBox = HitCharacter->HitCollisionBoxes[FName("head")];
	HeadBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	HeadBox->SetCollisionResponseToChannel(ECC_HitBox, ECollisionResponse::ECR_Block);

	FPredictProjectilePathParams PathParams;
	PathParams.bTraceWithCollision = true;
	PathParams.MaxSimTime = MaxRecordTime;
	PathParams.LaunchVelocity = InitialVelocity;
	PathParams.StartLocation = TraceStart;
	PathParams.SimFrequency = 15.f;
	PathParams.ProjectileRadius = 5.f;
	PathParams.TraceChannel = ECC_HitBox;
	PathParams.ActorsToIgnore.Add(GetOwner());

	FPredictProjectilePathResult PathResult;
	UGameplayStatics::PredictProjectilePath(this, PathParams, PathResult);

	if (PathResult.HitResult.bBlockingHit) //击中头部
	{
		ResetHitBoxes(HitCharacter, CurrentFrame);
		EnableCharacterMeshCollision(HitCharacter, ECollisionEnabled::QueryAndPhysics);
		return FServerSideRewindResult{true, true};
	}

	//未命中头部
	for (auto& HitBoxPair : HitCharacter->HitCollisionBoxes)
	{
		if (HitBoxPair.Value != nullptr)
		{
			HitBoxPair.Value->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			HitBoxPair.Value->SetCollisionResponseToChannel(ECC_HitBox, ECollisionResponse::ECR_Block);
		}
	}

	UGameplayStatics::PredictProjectilePath(this, PathParams, PathResult);
	if (PathResult.HitResult.bBlockingHit)
	{
		ResetHitBoxes(HitCharacter, CurrentFrame);
		EnableCharacterMeshCollision(HitCharacter, ECollisionEnabled::QueryAndPhysics);
		return FServerSideRewindResult{true, false};
	}

	ResetHitBoxes(HitCharacter, CurrentFrame);
	EnableCharacterMeshCollision(HitCharacter, ECollisionEnabled::QueryAndPhysics);
	return FServerSideRewindResult{false, false};
}

/**
 * @brief 缓存命中框位置到帧包
 * @param HitCharacter 角色
 * @param OutFramePackage 输出帧包
 */
void ULagCompensationComponent::CacheBoxPositions(ABrokenEdgeCharacter* HitCharacter, FFramePackage& OutFramePackage)
{
	if (HitCharacter == nullptr) return;
	for (auto& HitBoxPair : HitCharacter->HitCollisionBoxes)
	{
		if (HitBoxPair.Value != nullptr)
		{
			FBoxInformation BoxInfo;
			BoxInfo.Location = HitBoxPair.Value->GetComponentLocation();
			BoxInfo.Rotation = HitBoxPair.Value->GetComponentRotation();
			BoxInfo.BoxExtent = HitBoxPair.Value->GetScaledBoxExtent();
			OutFramePackage.HitBoxInfo.Add(HitBoxPair.Key, BoxInfo);
		}
	}
}

/**
 * @brief 将命中框移动到帧包记录的位置
 * @param HitCharacter 角色
 * @param FramePackage 帧包
 */
void ULagCompensationComponent::MoveBoxes(ABrokenEdgeCharacter* HitCharacter, const FFramePackage& FramePackage)
{
	if (HitCharacter == nullptr) return;

	for (auto& HitBoxPair : HitCharacter->HitCollisionBoxes)
	{
		if (HitBoxPair.Value != nullptr)
		{
			const FBoxInformation* BoxValue = FramePackage.HitBoxInfo.Find(HitBoxPair.Key);
			if (BoxValue)
			{
				HitBoxPair.Value->SetWorldLocation(BoxValue->Location);
				HitBoxPair.Value->SetWorldRotation(BoxValue->Rotation);
				HitBoxPair.Value->SetBoxExtent(BoxValue->BoxExtent);
			}
		}
	}
}


/**
 * @brief 启用/禁用角色网格碰撞
 * @param HitCharacter 角色
 * @param CollisionEnabled 碰撞状态
 */
void ULagCompensationComponent::EnableCharacterMeshCollision(ABrokenEdgeCharacter* HitCharacter,
                                                             ECollisionEnabled::Type CollisionEnabled)
{
	if (HitCharacter && HitCharacter->GetMesh())
	{
		HitCharacter->GetMesh()->SetCollisionEnabled(CollisionEnabled);
		HitCharacter->GetCapsuleComponent()->SetCollisionEnabled(CollisionEnabled);
	}
}


/**
 * @brief 重置命中框到原始位置
 * @param HitCharacter 角色
 * @param Package 原始帧包
 */
void ULagCompensationComponent::ResetHitBoxes(ABrokenEdgeCharacter* HitCharacter, const FFramePackage& Package)
{
	if (HitCharacter == nullptr) return;
	for (auto& HitBoxPair : HitCharacter->HitCollisionBoxes)
	{
		if (HitBoxPair.Value != nullptr)
		{
			const FBoxInformation* BoxValue = Package.HitBoxInfo.Find(HitBoxPair.Key);
			if (BoxValue)
			{
				HitBoxPair.Value->SetWorldLocation(BoxValue->Location);
				HitBoxPair.Value->SetWorldRotation(BoxValue->Rotation);
				HitBoxPair.Value->SetBoxExtent(BoxValue->BoxExtent);
				HitBoxPair.Value->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
	}
}


void ULagCompensationComponent::HitScanServerScoreRequest_Implementation(ABrokenEdgeCharacter* HitCharacter,
                                                                         const FVector_NetQuantize& TraceStart,
                                                                         const FVector_NetQuantize& HitLocation,
                                                                         float HitTime)
{
	FServerSideRewindResult Confirm = HitScanServerSideRewind(HitCharacter, TraceStart, HitLocation, HitTime);

	if (Character && HitCharacter && Character->GetEquippedWeapon() && Confirm.bHitConfirmed)
	{
		const float Damage = Confirm.bHeadShot
			                     ? Character->GetEquippedWeapon()->GetHeadShotDamage()
			                     : Character->GetEquippedWeapon()->GetDamage();

		UGameplayStatics::ApplyDamage(
			HitCharacter,
			Damage,
			Character->Controller,
			Character->GetEquippedWeapon(),
			UDamageType::StaticClass()
		);
	}
}


FServerSideRewindResult ULagCompensationComponent::HitScanServerSideRewind(ABrokenEdgeCharacter* HitCharacter,
                                                                           const FVector_NetQuantize& TraceStart,
                                                                           const FVector_NetQuantize& HitLocation,
                                                                           float HitTime)
{
	FFramePackage FrameToCheck = GetFrameToCheck(HitCharacter, HitTime);

	return HitScanConfirmHit(FrameToCheck, HitCharacter, TraceStart, HitLocation);
}

FServerSideRewindResult ULagCompensationComponent::HitScanConfirmHit(const FFramePackage& FramePackage,
                                                                     ABrokenEdgeCharacter* HitCharacter,
                                                                     const FVector_NetQuantize& TraceStart,
                                                                     const FVector_NetQuantize& HitLocation)
{
	if (HitCharacter == nullptr) return FServerSideRewindResult();

	FFramePackage CurrentFrame;
	CacheBoxPositions(HitCharacter, CurrentFrame);
	MoveBoxes(HitCharacter, FramePackage);
	EnableCharacterMeshCollision(HitCharacter, ECollisionEnabled::NoCollision);

	// 首先把头的命中框设置碰撞
	UBoxComponent* HeadBox = HitCharacter->HitCollisionBoxes[FName("head")];
	HeadBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	HeadBox->SetCollisionResponseToChannel(ECC_HitBox, ECollisionResponse::ECR_Block);

	FHitResult HitScanConfirmHitResult;
	const FVector TraceEnd = TraceStart + (HitLocation - TraceStart) * 1.25f;

	UWorld* World = GetWorld();
	if (World)
	{
		World->LineTraceSingleByChannel(
			HitScanConfirmHitResult,
			TraceStart,
			TraceEnd,
			ECC_HitBox
		);

		if (HitScanConfirmHitResult.bBlockingHit) //命中了头部
		{
			ResetHitBoxes(HitCharacter, CurrentFrame);
			EnableCharacterMeshCollision(HitCharacter, ECollisionEnabled::QueryAndPhysics);
			return FServerSideRewindResult{true, true};
		}
		else
		{
			for (auto& HitBoxPair : HitCharacter->HitCollisionBoxes)
			{
				if (HitBoxPair.Value != nullptr)
				{
					HitBoxPair.Value->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
					HitBoxPair.Value->SetCollisionResponseToChannel(ECC_HitBox, ECollisionResponse::ECR_Block);
				}
			}

			World->LineTraceSingleByChannel(
				HitScanConfirmHitResult,
				TraceStart,
				TraceEnd,
				ECC_HitBox
			);

			if (HitScanConfirmHitResult.bBlockingHit) //命中了头部以外的其他部位
			{
				ResetHitBoxes(HitCharacter, CurrentFrame);
				EnableCharacterMeshCollision(HitCharacter, ECollisionEnabled::QueryAndPhysics);
				return FServerSideRewindResult{true, false};
			}
		}
	}

	//什么也没命中
	ResetHitBoxes(HitCharacter, CurrentFrame);
	EnableCharacterMeshCollision(HitCharacter, ECollisionEnabled::QueryAndPhysics);
	return FServerSideRewindResult{true, false};
}

void ULagCompensationComponent::ShotgunServerScoreRequest_Implementation(
	const TArray<ABrokenEdgeCharacter*>& HitCharacters, const FVector& TraceStart,
	const TArray<FVector_NetQuantize>& HitLocations, float HitTime)
{
	FShotgunServerSideRewindResult Confirm = ShotgunServerSideRewind(HitCharacters, TraceStart, HitLocations, HitTime);

	for (auto& HitCharacter : HitCharacters)
	{
		if (HitCharacter == nullptr || Character == nullptr || Character->GetEquippedWeapon() == nullptr)continue;
		float TotalDamage = 0;
		if (Confirm.HeadShots.Contains(HitCharacter))
		{
			TotalDamage += Confirm.HeadShots[HitCharacter] * Character->GetEquippedWeapon()->GetHeadShotDamage();
		}

		if (Confirm.BodyShots.Contains(HitCharacter))
		{
			TotalDamage += Confirm.BodyShots[HitCharacter] * Character->GetEquippedWeapon()->GetDamage();
		}

		UGameplayStatics::ApplyDamage(
			HitCharacter,
			TotalDamage,
			Character->Controller,
			Character->GetEquippedWeapon(),
			UDamageType::StaticClass()
		);
	}
}


FShotgunServerSideRewindResult ULagCompensationComponent::ShotgunServerSideRewind(
	const TArray<ABrokenEdgeCharacter*>& HitCharacters, const FVector& TraceStart,
	const TArray<FVector_NetQuantize>& HitLocations, float HitTime)
{
	TArray<FFramePackage> FrameToChecks;

	for (auto& HitCharacter : HitCharacters)
	{
		FrameToChecks.Add(GetFrameToCheck(HitCharacter, HitTime));
	}

	return ShotgunConfirmHit(FrameToChecks, TraceStart, HitLocations);
}

FShotgunServerSideRewindResult ULagCompensationComponent::ShotgunConfirmHit(const TArray<FFramePackage>& FramePackages,
                                                                            const FVector& TraceStart,
                                                                            const TArray<FVector_NetQuantize>&
                                                                            HitLocations)
{
	for (auto& Frame : FramePackages)
	{
		if (Frame.Character == nullptr) return FShotgunServerSideRewindResult();
	}

	FShotgunServerSideRewindResult ShotgunResult;

	TArray<FFramePackage> CurrentFrames;

	for (auto& Frame : FramePackages)
	{
		FFramePackage CurrentFrame;
		CurrentFrame.Character = Frame.Character;
		CacheBoxPositions(Frame.Character, CurrentFrame);
		CurrentFrames.Add(CurrentFrame);

		MoveBoxes(Frame.Character, Frame);
		EnableCharacterMeshCollision(Frame.Character, ECollisionEnabled::NoCollision);
	}


	for (auto& Frame : FramePackages)
	{
		for (auto& HitBoxPair : Frame.Character->HitCollisionBoxes)
		{
			if (HitBoxPair.Value != nullptr)
			{
				HitBoxPair.Value->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				HitBoxPair.Value->SetCollisionResponseToChannel(ECC_HitBox, ECollisionResponse::ECR_Block);
			}
		}
	}

	for (auto& HitLocation : HitLocations)
	{
		FHitResult ConfirmHitResult;

		const FVector TraceEnd = TraceStart + (HitLocation - TraceStart) * 1.25f;

		if (UWorld* World = GetWorld())
		{
			World->LineTraceSingleByChannel(
				ConfirmHitResult,
				TraceStart,
				TraceEnd,
				ECC_HitBox
			);

			if (ABrokenEdgeCharacter* HitCharacter = Cast<ABrokenEdgeCharacter>(ConfirmHitResult.GetActor()))
			{
				UBoxComponent* HitBox = Cast<UBoxComponent>(ConfirmHitResult.GetComponent());
				const bool bHead = (HitBox == HitCharacter->HitCollisionBoxes[FName("head")]);
				if (bHead)
				{
					// ShotgunResult.HeadShots[HitCharacter]++;
					if (ShotgunResult.HeadShots.Contains(HitCharacter))
					{
						ShotgunResult.HeadShots[HitCharacter]++;
					}
					else
					{
						ShotgunResult.HeadShots.Emplace(HitCharacter,1);
					}

				}
				else
				{
					// ShotgunResult.BodyShots[HitCharacter]++;
					if (ShotgunResult.BodyShots.Contains(HitCharacter))
					{
						ShotgunResult.BodyShots[HitCharacter]++;
					}
					else
					{
						ShotgunResult.BodyShots.Emplace(HitCharacter,1);
					}
				}
			}
		}
	}

	for (auto& Frame : CurrentFrames)
	{
		ResetHitBoxes(Frame.Character, Frame);
		EnableCharacterMeshCollision(Frame.Character, ECollisionEnabled::QueryAndPhysics);
	}


	return ShotgunResult;
}
