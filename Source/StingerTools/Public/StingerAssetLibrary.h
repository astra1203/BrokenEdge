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
	UFUNCTION(BlueprintCallable, Category = "Stinger|Setup")
	static bool AssignMeshSkeleton(USkeletalMesh* Mesh, class USkeleton* Skeleton);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Setup")
	static void ReattachComponent(USceneComponent* Component, USceneComponent* Parent, FName SocketName);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static void BeginValidationPIE(int32 Players);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static TArray<UWorld*> GetValidationWorlds();
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static void InjectValidationInput(APlayerController* Controller, class UInputAction* Action, FVector Value);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static FString GetValidationState(APawn* Pawn);
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
