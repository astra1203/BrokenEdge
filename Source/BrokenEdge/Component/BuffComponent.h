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
	void ShieldRampUp(float DeltaTime);

private:
	/** @brief 所属角色指针 */
	UPROPERTY()
	TObjectPtr<ABrokenEdgeCharacter> Character;

	
	bool bHealing = false;/** @brief 是否正在进行治疗 */
	float HealingRate = 0.f;/** @brief 每秒治疗量 */
	float AmountToHeal = 0.f;/** @brief 剩余治疗总量 */
	
	/**
     * @brief 护盾增益
    */
	bool bReplenishingShield = false;      ///< 是否正在恢复护盾
	float ShieldReplenishRate = 0.f;       ///< 护盾恢复速率
	float ShieldReplenishAmount = 0.f;     ///< 剩余护盾恢复量

	
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
	
	
	/**
     * @brief 护盾恢复
     * @param ShieldAmount 恢复总量
     * @param ReplenishTime 恢复时间
     */
	void ReplenishShield(float ShieldAmount, float ReplenishTime);

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
