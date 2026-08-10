// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"
#include "GameFramework/Character.h"
#include "BrokenEdgeCharacter.generated.h"

class UInputAction;
class UInputMappingContext;

UCLASS()
class BROKENEDGE_API ABrokenEdgeCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	ABrokenEdgeCharacter();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	
	
	virtual void NotifyControllerChanged() override;
	
	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

private:
	/** MappingContext */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputMappingContext* DefaultMappingContext;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* MoveAction;
	
	/** Look Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* LookAction;

	/// 摄像机弹簧臂组件
	UPROPERTY(VisibleAnywhere, Category = Camera)
	class USpringArmComponent* CameraBoom;

	/// 跟随摄像机组件
	UPROPERTY(VisibleAnywhere, Category = Camera)
	class UCameraComponent* FollowCamera;
	
	/** 头顶UI组件 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class UWidgetComponent* OverheadWidget;
	
	/** 战斗组件 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class UCombatComponent* Combat;

	/** Buff组件 */
	UPROPERTY(VisibleAnywhere)
	class UBuffComponent* Buff;

	/** 是否旋转根骨骼 */
	bool bRotateRootBone = false;
	
	/** 是否被淘汰 */
	bool bElimmed = false;
	
public:

	/** 检查查是否装备了武器 */
	bool IsWeaponEquipped();
	
	//是否处于瞄准状态
	bool IsAiming();
	
	/** 是否应该旋转根骨骼 */  //inline
	FORCEINLINE bool ShouldRotateRootBone() const { return bRotateRootBone; }
	
	/** 是否被淘汰 */
	FORCEINLINE bool IsElimed() const { return bElimmed; }

};
