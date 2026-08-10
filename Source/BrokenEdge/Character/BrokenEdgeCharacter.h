// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"
#include "GameFramework/Character.h"
#include "BrokenEdgeCharacter.generated.h"

class UBoxComponent;
enum class ETurningInPlace : uint8;
class UInputAction;
class UInputMappingContext;

UCLASS()
class BROKENEDGE_API ABrokenEdgeCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	ABrokenEdgeCharacter();

	/** 命中碰撞盒映射 */  // unordered_map
	UPROPERTY()
	TMap<FName, UBoxComponent*> HitCollisionBoxes;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	/** 头部碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* head = nullptr;

	/** 骨盆碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* pelvis = nullptr;

	/** 第二节脊椎碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* spine_02 = nullptr;

	/** 第三节脊椎碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* spine_03 = nullptr;

	/** 左上臂碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* upperarm_l = nullptr;

	/** 右上臂碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* upperarm_r = nullptr;

	/** 左前臂碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* lowerarm_l = nullptr;

	/** 右前臂碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* lowerarm_r = nullptr;

	/** 左手碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* hand_l = nullptr;

	/** 右手碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* hand_r = nullptr;

	/** 背包碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* backpack = nullptr;

	/** 毯子碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* blanket = nullptr;

	/** 左大腿碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* thigh_l = nullptr;

	/** 右大腿碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* thigh_r = nullptr;

	/** 左小腿碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* calf_l = nullptr;

	/** 右小腿碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* calf_r = nullptr;

	/** 左脚碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* foot_l = nullptr;

	/** 右脚碰撞盒 */
	UPROPERTY(EditAnywhere)
	UBoxComponent* foot_r = nullptr;
	
public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	virtual void NotifyControllerChanged() override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void Jump() override;
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void CrouchButtonPressed();

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
	
	/** Crouch Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* CrouchAction;

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
	
	/// 原地转向状态
	ETurningInPlace TurningInPlace;
	
public:
	/** 是否禁用游戏逻辑 */
	// UPROPERTY(Replicated)
	// bool bDisableGameplay = false;

	/** 检查查是否装备了武器 */
	bool IsWeaponEquipped();
	
	//是否处于瞄准状态
	bool IsAiming();
	
	/** 是否应该旋转根骨骼 */  //inline
	FORCEINLINE bool ShouldRotateRootBone() const { return bRotateRootBone; }
	
	/** 是否被淘汰 */
	FORCEINLINE bool IsElimed() const { return bElimmed; }

};
