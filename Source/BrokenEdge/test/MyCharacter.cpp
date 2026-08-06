// Fill out your copyright notice in the Description page of Project Settings.


#include "MyCharacter.h"


// Sets default values
AMyCharacter::AMyCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void AMyCharacter::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AMyCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void AMyCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

void AMyCharacter::TakeDamage(float DamageAmount)
{
	if (DamageAmount<=0.0f)
	{
		return;	
	}
	// Health -= DamageAmount;
	const float OldHealth = Health;
	Health = FMath::Clamp(Health - DamageAmount, 0.0f, 100.0f);
	const float HealthDelta = Health - OldHealth;

	if (!FMath::IsNearlyZero(HealthDelta))
	{
		OnHealthChanged.Broadcast(Health, HealthDelta);
	}
}

