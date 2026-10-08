#include "ExtractionLadder.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"

AExtractionLadder::AExtractionLadder()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	bReplicates = true;
	bAlwaysRelevant = true;
}

void AExtractionLadder::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const
{
	Super::GetLifetimeReplicatedProps(Out);
	auto& OutLifetimeProps = Out;
	DOREPLIFETIME(AExtractionLadder, Occupant);
	DOREPLIFETIME(AExtractionLadder, SourceId);
	DOREPLIFETIME(AExtractionLadder, Bottom);
	DOREPLIFETIME(AExtractionLadder, Top);
	DOREPLIFETIME(AExtractionLadder, BottomExit);
	DOREPLIFETIME(AExtractionLadder, TopExit);
	DOREPLIFETIME(AExtractionLadder, GripOrigin);
	DOREPLIFETIME(AExtractionLadder, RungSpacing);
	DOREPLIFETIME(AExtractionLadder, GripHalfWidth);
	DOREPLIFETIME(AExtractionLadder, Rungs);
}

bool AExtractionLadder::Claim(ACharacter* Character)
{
	if (!HasAuthority() || !IsValid(Character) || (IsValid(Occupant) && Occupant != Character)) return false;
	Occupant = Character;
	ForceNetUpdate();
	return true;
}

void AExtractionLadder::Release(ACharacter* Character)
{
	if (HasAuthority() && Occupant == Character)
	{
		Occupant = nullptr;
		ForceNetUpdate();
	}
}
