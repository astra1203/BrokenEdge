#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "StingerAssetLibrary.generated.h"

class UAnimSequence;
class UAnimBlueprint;
class UAnimMontage;
class USkeletalMesh;

/** 编辑器专用，补齐未导出到 Python 的动画资产操作。 */
UCLASS()
class STINGERTOOLS_API UStingerAssetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Stinger|Import")
	static class USkeleton* CreateSourceSkeleton(const FString& PackageName, const TArray<FName>& Names,
	                                             const TArray<int32>& Parents, const TArray<FTransform>& ReferencePose);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Setup")
	static bool AssignMeshSkeleton(USkeletalMesh* Mesh, class USkeleton* Skeleton);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Setup")
	static void ReattachComponent(USceneComponent* Component, USceneComponent* Parent, FName SocketName);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static void BeginValidationPIE(int32 Players);
	/** Fix the render target size without changing the user's saved PIE settings. */
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static bool SetValidationViewportSize(APlayerController* Controller, int32 Width, int32 Height);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static TArray<UWorld*> GetValidationWorlds();
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static void IsolateValidationInput(APlayerController* Controller);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static void InjectValidationInput(APlayerController* Controller, class UInputAction* Action, FVector Value);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static FString GetValidationState(APawn* Pawn);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static FString DescribeThirdPersonGripIK(UAnimBlueprint* Blueprint);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Setup")
	static bool ConfigureThirdPersonGripIK(UAnimBlueprint* Blueprint);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Setup")
	static bool ConfigureTraversalSlot(UAnimBlueprint* Blueprint);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static FString DescribeLadderVertices(class UStaticMesh* Mesh);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static TArray<FVector> TraceTraversalSurface(UObject* WorldContext, FVector Start, FVector End);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static bool TraversalCapsuleClear(UObject* WorldContext, FVector Start, FVector End, float Radius,
	                                  float HalfHeight);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static void SetValidationFixedStep(float Seconds);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static AActor* SpawnValidationWeapon(UWorld* World, TSubclassOf<AActor> Class, FVector Location);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static bool DropValidationWeapon(APawn* Pawn);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static FString GetMontageDescription(UAnimMontage* Montage);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Import")
	static void ResetAnimation(UAnimSequence* Sequence);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Import")
	static void FinalizeAnimation(UAnimSequence* Sequence);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Import")
	static void AddMeshSocket(USkeletalMesh* Mesh, FName SocketName, FName BoneName, FTransform Offset);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Import")
	static void RemapBlueprintBones(UAnimBlueprint* Blueprint, const TMap<FName, FName>& BoneMap);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Import")
	static UAnimMontage* CreateVisualMontage(UAnimSequence* Sequence, const FString& PackageName);
};
