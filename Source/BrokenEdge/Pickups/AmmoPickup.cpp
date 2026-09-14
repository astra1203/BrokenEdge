// Fill out your copyright notice in the Description page of Project Settings.


#include "AmmoPickup.h"

#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/CombatComponent.h"


void AAmmoPickup::OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
                                  UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
                                  const FHitResult& SweepResult)
{

	UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s"), GFrameCounter, TEXT(__FUNCTION__), __LINE__, TEXT(""));
	if (ABrokenEdgeCharacter* BrokenEdgeCharacter = Cast<ABrokenEdgeCharacter>(OtherActor))
	{
		if (UCombatComponent* CombatComponent = BrokenEdgeCharacter->GetCombat())
		{
			AmmoAmount = CombatComponent->PickupAmmo(WeaponType, AmmoAmount);
		}
	}
	
	if (AmmoAmount <= 0)
	{
		Destroy();
	}
}
