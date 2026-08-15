// Fill out your copyright notice in the Description page of Project Settings.


#include "TestActorMove.h"


// Sets default values
ATestActorMove::ATestActorMove()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void ATestActorMove::BeginPlay()
{
	Super::BeginPlay();

	StartLocation = GetActorLocation();
	EndLocation = StartLocation + GetActorForwardVector() * MoveDistance;
}

// Called every frame
void ATestActorMove::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const FVector TargetLocation = bMovingToEnd ? EndLocation : StartLocation;
	const FVector NewLocation = FMath::VInterpConstantTo(
		GetActorLocation(),
		TargetLocation,
		DeltaTime,
		MoveSpeed
	);

	SetActorLocation(NewLocation);

	if (NewLocation.Equals(TargetLocation, 1.f))
	{
		bMovingToEnd = !bMovingToEnd;
	}
}
