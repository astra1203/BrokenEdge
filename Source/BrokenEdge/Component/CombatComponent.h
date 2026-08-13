// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BrokenEdge/BrokenEdgeTypes/CombatState.h"
#include "Components/ActorComponent.h"
#include "CombatComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class BROKENEDGE_API UCombatComponent : public UActorComponent
{
	GENERATED_BODY()

	friend class ABrokenEdgeCharacter;

public:	
	// Sets default values for this component's properties
	UCombatComponent();

	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	/**
 * @brief 装备武器
 * @param WeaponToEquip 要装备的武器
 */
	void EquipWeapon(class AWeapon* WeaponToEquip);

	// Called when the game starts
	virtual void BeginPlay() override;


public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	/**
 * @brief 掉落当前装备的武器
 */
	void DropEquippedWeapon();

	/**
 * @brief 将旗帜附加到左手
 * @param Flag 要附加的旗帜
 */
	void AttachFlagToLeftHand(AActor* Flag);

	/**
 * @brief 将Actor附加到右手
 * @param ActorToAttach 要附加的Actor
 */
	void AttachActorToRightHand(AActor* ActorToAttach);

	
	/**
	 * @brief 将Actor附加到背包
	 * @param ActorToAttach 要附加的Actor
	 */
	void AttachActorToBackpack(AActor* ActorToAttach);

	
	/**
 * @brief 装备主武器
 * @param WeaponToEquip 要装备的武器
 */
	void EquipPrimaryWeapon(AWeapon* WeaponToEquip);

	/**
 * @brief 装备副武器
 * @param WeaponToEquip 要装备的武器
 */
	void EquipSecondaryWeapon(AWeapon* WeaponToEquip);

private:
	/// 关联的角色
	UPROPERTY()
	class ABrokenEdgeCharacter* Character;

	/** 当前装备的武器 */
	UPROPERTY(ReplicatedUsing = OnRep_EquippedWeapon)
	class AWeapon* EquippedWeapon;

	/** 副武器 */
	UPROPERTY(ReplicatedUsing = OnRep_SecondaryWeapon)
	AWeapon* SecondaryWeapon;

	/**
 * @brief 装备武器复制回调
 */
	UFUNCTION()
	void OnRep_EquippedWeapon();

	/**
	 *
	 * @brief 副武器复制回调
	 */
	UFUNCTION()
	void OnRep_SecondaryWeapon();

	/** 是否持有旗帜 */
	UPROPERTY(ReplicatedUsing = OnRep_HoldingTheFlag)
	bool bHoldingTheFlag = false;

	/**
    * @brief 持有旗帜状态复制回调
   */
	UFUNCTION()
	void OnRep_HoldingTheFlag();

	/// 战斗状态（带复制通知）
	UPROPERTY(ReplicatedUsing = OnRep_CombatState)
	ECombatState CombatState = ECombatState::ECS_Unoccuiped;

	/**
 * @brief 战斗状态复制回调
 */
	UFUNCTION()
	void OnRep_CombatState();

	/// 旗帜武器（带复制通知）
	UPROPERTY(ReplicatedUsing = OnRep_Flag)
	AWeapon* TheFlag;

	/**
	 * @brief 旗帜复制回调
	 */
	UFUNCTION()
	void OnRep_Flag();


public:
	/**
	 * @brief 检查是否应该切换武器
	 * @return 是否应该切换
	 */
	bool ShouldSwapWeapons();
};
