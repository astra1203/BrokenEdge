// Fill out your copyright notice in the Description page of Project Settings.


#include "BuffComponent.h"

#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"


// Sets default values for this component's properties
UBuffComponent::UBuffComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}


// Called when the game starts
void UBuffComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...

}
/**
 * @brief 每帧治疗更新
 * @param DeltaTime 帧时间
 */
void UBuffComponent::HealRampUp(float DeltaTime)
{
	if (!bHealing || Character == nullptr || !Character->HasAuthority() || Character->IsDefeated()
		|| !FMath::IsFinite(DeltaTime) || DeltaTime <= 0.f) return;
	const float HealThisFrame = FMath::Min(HealingRate * DeltaTime, AmountToHeal);
	Character->ApplyVitalsChange({HealThisFrame});
	AmountToHeal -= HealThisFrame;

	if (AmountToHeal <= 0.f || Character->GetHealth() >= Character->GetMaxHealth())
	{
		bHealing = false;
		AmountToHeal = 0.f;
	}
}

void UBuffComponent::ShieldRampUp(float DeltaTime)
{
	if (!bReplenishingShield || Character == nullptr || !Character->HasAuthority() || Character->IsDefeated()
		|| !FMath::IsFinite(DeltaTime) || DeltaTime <= 0.f) return;
	const float ReplenishThisFrame = FMath::Min(ShieldReplenishRate * DeltaTime, ShieldReplenishAmount);
	Character->ApplyVitalsChange({0, ReplenishThisFrame});
	ShieldReplenishAmount -= ReplenishThisFrame;
	if (ShieldReplenishAmount <= 0.f || Character->GetShield() >= Character->GetMaxShield())
	{
		bReplenishingShield = false;
		ShieldReplenishAmount = 0.f;
	}
}


// Called every frame
void UBuffComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	HealRampUp(DeltaTime);
	ShieldRampUp(DeltaTime);
}

void UBuffComponent::BuffJump(float BuffJumpVelocity, float BuffTime)
{
	if (Character == nullptr) return;
	Character->GetWorldTimerManager().SetTimer(
		JumpBuffTimer,
		this,
		&UBuffComponent::ResetJump,
		BuffTime
	);
	if (Character->GetCharacterMovement())
	{
		Character->GetCharacterMovement()->JumpZVelocity = BuffJumpVelocity;
	}

	MulticastJumpBuff(BuffJumpVelocity);

}

/**
 * @brief 多播跳跃增益效果到所有客户端
 * @param JumpVelocity 跳跃速度
 */
void UBuffComponent::MulticastJumpBuff_Implementation(float JumpVelocity)
{
	if (Character && Character->GetCharacterMovement())
	{
		Character->GetCharacterMovement()->JumpZVelocity = JumpVelocity;
	}
}

void UBuffComponent::ResetJump()
{
	if (Character == nullptr) return;
	
	if (Character->GetCharacterMovement())
	{
		Character->GetCharacterMovement()->JumpZVelocity = InitialJumpZVelocity;
	}

	MulticastJumpBuff(InitialJumpZVelocity);

}


void UBuffComponent::BuffSpeed(float BuffBaseSpeed, float BuffCrouchSpeed, float BuffTime)
{
	if (Character == nullptr) return;

	Character->GetWorldTimerManager().SetTimer(
		SpeedBuffTimer,
		this,
		&UBuffComponent::ResetSpeeds,
		BuffTime
	);

	if (Character->GetCharacterMovement())
	{
		Character->GetCharacterMovement()->MaxWalkSpeed = BuffBaseSpeed;
		Character->GetCharacterMovement()->MaxWalkSpeedCrouched = BuffCrouchSpeed;
	}
	MulticastSpeedBuff(BuffBaseSpeed, BuffCrouchSpeed);

}


/**
 * @brief 重置移动速度为初始值
 */
void UBuffComponent::ResetSpeeds()
{
	if (Character == nullptr || Character->GetCharacterMovement() == nullptr) return;
	Character->GetCharacterMovement()->MaxWalkSpeed = InitialBaseSpeed;
	Character->GetCharacterMovement()->MaxWalkSpeedCrouched = InitialCrouchSpeed;

	MulticastSpeedBuff(InitialBaseSpeed, InitialCrouchSpeed);
}

/**
 * @brief 多播速度增益效果到所有客户端
 * @param BaseSpeed 基础速度
 * @param CrouchSpeed 蹲伏速度
 */
void UBuffComponent::MulticastSpeedBuff_Implementation(float BaseSpeed, float CrouchSpeed)
{
	Character->GetCharacterMovement()->MaxWalkSpeed = BaseSpeed;
	Character->GetCharacterMovement()->MaxWalkSpeedCrouched = CrouchSpeed;
}

void UBuffComponent::BuffHeal(float HealAmount, float HealingTime)
{
	if (!Character || !Character->HasAuthority() || Character->IsDefeated()
		|| !FMath::IsFinite(HealAmount) || !FMath::IsFinite(HealingTime) || HealAmount <= 0.f || HealingTime <= 0.f) return;
	const float Rate = HealAmount / HealingTime;
	const float Pending = AmountToHeal + HealAmount;
	if (!FMath::IsFinite(Rate) || Rate <= 0.f || !FMath::IsFinite(Pending)) return;
	bHealing = true;
	HealingRate = Rate;
	AmountToHeal = Pending;
}

void UBuffComponent::ReplenishShield(float ShieldAmount, float ReplenishTime)
{
	if (!Character || !Character->HasAuthority() || Character->IsDefeated()
		|| !FMath::IsFinite(ShieldAmount) || !FMath::IsFinite(ReplenishTime) || ShieldAmount <= 0.f || ReplenishTime <= 0.f) return;
	const float Rate = ShieldAmount / ReplenishTime;
	const float Pending = ShieldReplenishAmount + ShieldAmount;
	if (!FMath::IsFinite(Rate) || Rate <= 0.f || !FMath::IsFinite(Pending)) return;
	bReplenishingShield = true;
	ShieldReplenishRate = Rate;
	ShieldReplenishAmount = Pending;
}
