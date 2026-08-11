// Fill out your copyright notice in the Description page of Project Settings.


#include "JumpPickup.h"

#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/BuffComponent.h"

//
// void AJumpPickup::BeginPlay()
// {
// 	Super::BeginPlay();
// }

void AJumpPickup::OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
                                  UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	Super::OnSphereOverlap(OverlappedComponent, OtherActor, OtherComp, OtherBodyIndex, bFromSweep, SweepResult);

	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT(""));
	if (TObjectPtr<ABrokenEdgeCharacter> BrokenEdgeCharacter = Cast<ABrokenEdgeCharacter>(OtherActor))
	{
		if (TObjectPtr<UBuffComponent> BuffComponent = BrokenEdgeCharacter->GetBuff())
		{


			BuffComponent->BuffJump(JumpZVelocityBuff, JumpBuffTime);
			Destroy();
		}
	}


}
