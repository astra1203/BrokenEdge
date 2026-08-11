// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BuffComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class BROKENEDGE_API UBuffComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UBuffComponent();

	friend class ABrokenEdgeCharacter;

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

private:
	/** @brief 所属角色指针 */
	UPROPERTY()
	TObjectPtr<ABrokenEdgeCharacter> Character;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	* @brief 设置跳跃增益
	* @param BuffJumpVelocity 增益后的跳跃速度
	* @param BuffTime 增益持续时间（秒）
	*/
	void BuffJump(float BuffJumpVelocity, float BuffTime);

private:
	/**
	* @brief 跳跃增益
	*/
	FTimerHandle JumpBuffTimer;             ///< 跳跃增益计时器

	void ResetJump();
	float InitialJumpZVelocity;              ///< 初始跳跃速度
};
