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
	void HealRampUp(float DeltaTime);

private:
	/** @brief 所属角色指针 */
	UPROPERTY()
	TObjectPtr<ABrokenEdgeCharacter> Character;

	/** @brief 是否正在进行治疗 */
	bool bHealing = false;
	
	/** @brief 每秒治疗量 */
	float HealingRate = 0.f;

	/** @brief 剩余治疗总量 */
	float AmountToHeal = 0.f;
	
public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	* @brief 设置跳跃增益
	* @param BuffJumpVelocity 增益后的跳跃速度
	* @param BuffTime 增益持续时间（秒）
	*/
	void BuffJump(float BuffJumpVelocity, float BuffTime);


	/**
	 * @brief 设置速度增益
	 * @param BuffBaseSpeed 增益后的基础移动速度
	 * @param BuffCrouchSpeed 增益后的蹲伏移动速度
	 * @param BuffTime 增益持续时间（秒）
	 */
	void BuffSpeed(float BuffBaseSpeed, float BuffCrouchSpeed, float BuffTime);
	
	/**
	 * @brief 开始治疗
	 * @param HealAmount 治疗总量
	 * @param HealingTime 治疗持续时间
	 */
	void BuffHeal(float HealAmount, float HealingTime);



private:
	/**
	* @brief 跳跃增益
	*/
	FTimerHandle JumpBuffTimer;             ///< 跳跃增益计时器

	/** @brief 速度增益计时器句柄 */
	FTimerHandle SpeedBuffTimer;

	void ResetJump();
	void ResetSpeeds();

	float InitialJumpZVelocity;              ///< 初始跳跃速度

	/** @brief 初始基础移动速度 */
	float InitialBaseSpeed;

	/** @brief 初始蹲伏移动速度 */
	float InitialCrouchSpeed;
};
