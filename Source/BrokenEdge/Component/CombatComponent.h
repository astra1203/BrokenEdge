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

	// Called when the game starts
	virtual void BeginPlay() override;

	
public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
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
};
