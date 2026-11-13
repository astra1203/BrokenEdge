#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CharacterPresentationTypes.h"
#include "BrokenEdgeCharacterPresentation.generated.h"

class UAnimInstance;
class UAnimMontage;
class UAnimSequence;
class USkeletalMesh;

/** Character model contract. Gameplay state and weapon-specific presentation are not stored here. */
UCLASS(BlueprintType)
class BROKENEDGE_API UBrokenEdgeCharacterPresentation : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USkeletalMesh> WorldMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSubclassOf<UAnimInstance> WorldAnimClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FTransform WorldTransform;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USkeletalMesh> FirstPersonMeshAsset;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSubclassOf<UAnimInstance> FirstPersonAnimClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FTransform FirstPersonMeshTransform = FTransform(FRotator(0, -90, 0), FVector(-10, 0, -165));
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector FirstPersonAimMeshOffset = FVector(20, 0, 14);
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USkeletalMesh> FirstPersonWeaponSourceMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USkeletalMesh> FirstPersonWeaponVisualMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FStingerViewWeaponFit FirstPersonWeaponFit;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FTransform FirstPersonWeaponTransform;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAnimMontage> FirstPersonFireMontage;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAnimMontage> FirstPersonReloadMontage;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAnimMontage> FirstPersonSwapMontage;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> StandToProneAnim;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> ProneIdleAnim;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> ProneToStandAnim;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> ProneFireAnim;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> ProneReloadAnim;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName FirstPersonHeadBone = TEXT("head");
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FName> FirstPersonHiddenMaterialSlots;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector FirstPersonCameraOffset = FVector(10, 0, 0);
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FName> NeckBoneCandidates = {TEXT("Neck"), TEXT("neck_01")};
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName WeaponHandSocket = TEXT("RightHandSocket");
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName WeaponBackSocket = TEXT("BackpackSocket");
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName FirstPersonWeaponSocket = TEXT("RightHandSocket");
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FName> RequiredFirstPersonBones = {TEXT("RightHand"), TEXT("LeftHand")};
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FName> HeadBones = {TEXT("head")};
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FName> TorsoBones = {TEXT("pelvis"), TEXT("Hips")};
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FString> TorsoPrefixes = {TEXT("spine")};
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FName> LegBones = {TEXT("LeftUpLeg"), TEXT("RightUpLeg"), TEXT("LeftLeg"), TEXT("RightLeg"), TEXT("LeftFoot"), TEXT("RightFoot"), TEXT("LeftToeBase"), TEXT("RightToeBase")};
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FString> LegPrefixes = {TEXT("thigh_"), TEXT("calf_"), TEXT("foot_"), TEXT("ball_")};
    int32 ArmorSlotForBone(FName Bone) const;
    bool IsLeg(FName Bone) const;
    UFUNCTION(BlueprintCallable, Category="Validation") TArray<FString> ValidatePresentation() const;
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
