// Fill out your copyright notice in the Description page of Project Settings.


#include "HealthPickup.h"

#include "BrokenEdge/BrokenEdge.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/BuffComponent.h"
#include "BrokenEdge/Extraction/ExtractionState.h"


void AHealthPickup::OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
                                    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
                                    const FHitResult& SweepResult)
{
	if (GetWorld()->GetGameState<AExtractionGameState>()) return;
	Super::OnSphereOverlap(OverlappedComponent, OtherActor, OtherComp, OtherBodyIndex, bFromSweep, SweepResult);

	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__,
	       *PrintNetMode());

	if (TObjectPtr<ABrokenEdgeCharacter> BrokenEdgeCharacter = Cast<ABrokenEdgeCharacter>(OtherActor))
	{
		if (TObjectPtr<UBuffComponent> BuffComponent = BrokenEdgeCharacter->GetBuff())
		{
			BuffComponent->BuffHeal(HealthAmount, HealthingTime);
			Destroy();
		}
	}
}
