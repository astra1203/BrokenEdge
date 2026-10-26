#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RifleLocomotionSet.generated.h"

class UAnimSequence;
class UBlendSpace;

/** Opt-in presentation only; movement and combat remain authoritative elsewhere. */
UCLASS(BlueprintType)
class BROKENEDGE_API URifleLocomotionSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1"))
	float CrouchSpeed = 128.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1"))
	float SprintSpeed = 561.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UBlendSpace> Standing;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UBlendSpace> Crouching;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UBlendSpace> AimOffset;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UBlendSpace> CrouchAimOffset;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> ViewIdle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> ViewWalk;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> ViewRun;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> ViewSprint;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> WalkToRun;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> RunToWalk;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> WalkToSprint;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> RunToSprint;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> SprintToWalk;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1"))
	float WalkSpeed = 150.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1"))
	float RunSpeed = 600.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1"))
	float RunThreshold = 375.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	float SpeedHysteresis = 20.f;
};
