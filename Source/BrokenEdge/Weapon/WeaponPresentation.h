#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "WeaponPresentation.generated.h"

class UAnimSequence;

/** Per-weapon presentation; contains no inventory or authoritative hit decisions. */
UCLASS(BlueprintType)
class BROKENEDGE_API UWeaponPresentation : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<class URifleLocomotionSet> Locomotion;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<USkeletalMesh> ViewMesh;
	/** Optional first-person scope view for models with an opaque baked-in lens. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<class UTexture2D> ScopeOverlay;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="5", ClampMax="90"))
	float ScopeFOV = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FStingerViewWeaponFit ViewFit;
	/** Mesh-frame correction relative to the character's authored weapon sockets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform WorldHandTransform = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform WorldBackTransform = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> Idle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> Aim;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> Sprint;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> Jump;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimMontage> BodyFire;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimMontage> ViewFire;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimMontage> BodyReload;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimMontage> ViewReload;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimMontage> BodySwap;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimMontage> ViewSwap;
};
