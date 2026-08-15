// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TestActorMove.generated.h"

UCLASS()
class BROKENEDGE_API ATestActorMove : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ATestActorMove();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

private:
	/** Actor 沿自身前方移动的最大距离。 */
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.0"))
	float MoveDistance = 500.f;

	/** Actor 每秒移动的距离。 */
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.0"))
	float MoveSpeed = 200.f;

	FVector StartLocation;
	FVector EndLocation;
	bool bMovingToEnd = true;
};
