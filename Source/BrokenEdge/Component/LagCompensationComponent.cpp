// Fill out your copyright notice in the Description page of Project Settings.


#include "LagCompensationComponent.h"

#include "ScreenPass.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "Components/BoxComponent.h"


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
void ULagCompensationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
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
		while (HistoryLength>MaxRecordTime)
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
	
	for (auto& BoxPair: Character->HitCollisionBoxes)
	{
		FBoxInformation BoxInformation;
		BoxInformation.Location = BoxPair.Value->GetComponentLocation();
		BoxInformation.Rotation = BoxPair.Value->GetComponentRotation();
		BoxInformation.BoxExtent = BoxPair.Value->GetScaledBoxExtent();
		Package.HitBoxInfo.Add(BoxPair.Key, BoxInformation);
	}
	
	
}

void ULagCompensationComponent::ProjectileServerScoreRequest_Implementation(ABrokenEdgeCharacter* HitCharacter,
                  const FVector_NetQuantize& TraceStart, const FVector_NetQuantize100& InitialVelocity, float HitTime)
{
	FServerSideRewindResult Confirm = ProjectileServerSideRewind(HitCharacter, TraceStart, InitialVelocity, HitTime);

	
	
}

/**
 * @brief 执行服务器端倒带（投射物武器）
 * @param HitCharacter 被命中的角色
 * @param TraceStart 投射物起点
 * @param InitialVelocity 初始速度
 * @param HitTime 命中时间
 * @return 命中确认结果
 */
FServerSideRewindResult ULagCompensationComponent::ProjectileServerSideRewind(ABrokenEdgeCharacter* HitCharacter, const FVector_NetQuantize& TraceStart, const FVector_NetQuantize100& InitialVelocity, float HitTime)
{
	FFramePackage FrameToChrck = GetFrameToCheck(HitCharacter, HitTime);
	// return ProjectileConfirmHit(FrameToChrck, HitCharacter, TraceStart, InitialVelocity, HitTime);
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
	
	if (OldestHistoryTime>HitTime)
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
		if (Older->GetNextNode()==nullptr)break;
		Older = Older->GetNextNode();
		
		if (Older->GetValue().Time > HitTime)Younger=Older;
		
	}
	if (Older->GetValue().Time == HitTime)
	{
		FrameToCheck = Older->GetValue();
		bShouldInterpolate = false;
	}
	if (bShouldInterpolate)
	{
		
	}
	
	
}

FFramePackage ULagCompensationComponent::InterpBetweenFrames(const FFramePackage& OlderFrame,
	const FFramePackage& YoungerFrame, float HitTime)
{
	FFramePackage InterpFramePackage;
	InterpFramePackage.Time = HitTime;
	
	const float Distance = YoungerFrame.Time - OlderFrame.Time;
	const float InterpFraction = FMath::Clamp((HitTime - OlderFrame.Time) / Distance, 0.f, 1.f);

	for (auto& YoungerPair:YoungerFrame.HitBoxInfo)
	{
		const FName& BoxInfoName = YoungerPair.Key;
		
		const FBoxInformation& OlderBox = OlderFrame.HitBoxInfo[BoxInfoName];
		const FBoxInformation& YoungerBox = YoungerFrame.HitBoxInfo[BoxInfoName];
		
		FBoxInformation InterpBoxInfo;
		// InterpBoxInfo.Location 
		
		InterpFramePackage.HitBoxInfo.Add(BoxInfoName, InterpBoxInfo);
	}
	
	
	
	return InterpFramePackage;
}
