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
	UFUNCTION(BlueprintCallable, Category = "Stinger|Locomotion")
	static bool ConfigureRifleLocomotion(UAnimBlueprint* Blueprint);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Locomotion")
	static bool PopulateLocomotionBlendSpace(class UBlendSpace* BlendSpace, const TArray<UAnimSequence*>& Animations,
	                                         const TArray<FVector>& Coordinates, float MaxSpeed, bool bAimOffset);
	/** Drain async asset compilation before cold commandlet collision audits. */
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static void FinishValidationAssetCompilation();
	UFUNCTION(BlueprintCallable, Category = "Stinger|Import")
	static class USkeleton* CreateSourceSkeleton(const FString& PackageName, const TArray<FName>& Names,
	                                             const TArray<int32>& Parents, const TArray<FTransform>& ReferencePose);
	/** 从 TTF 文件生成字体：每个文件一个 Inline 字体面，按 Typefaces 名称组成一个运行时 UFont 并保存。 */
	UFUNCTION(BlueprintCallable, Category = "Stinger|UI")
	static class UFont* CreateFontFromFiles(const TArray<FString>& Files, const TArray<FName>& Typefaces,
	                                        const FString& PackagePath, const FString& AssetName);
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
	/** 骨骼网格 LOD0 参考姿势顶点（组件空间），JSON [[x,y,z],...]。 */
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static FString DescribeSkeletalVertices(class USkeletalMesh* Mesh);
	/** 诊断某个组件的碰撞：物理状态、实例体数量、网格碰撞几何，以及对它单独做一次线检测的结果。 */
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static FString DescribeComponentCollision(class UPrimitiveComponent* Component, FVector Start, FVector End);
	/** 实例化网格的 bDisableCollision 没有编辑器可见性，Python 改不到；这里清掉它并重建物理状态。 */
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static bool EnableInstancedCollision(class UInstancedStaticMeshComponent* Component);
	/**
	 * 把枪的骨骼网格按侧视图光栅化成物品图标 PNG（透明底、浅灰带明暗），枪口朝右。
	 * 长轴取 X/Y 中较长的那个；有 MuzzleFlash 插槽时据此判断枪口方向。返回写出的宽度，失败返回 0。
	 */
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static int32 RenderWeaponIcon(class USkeletalMesh* Mesh, const FString& File, int32 Height, float Margin = .06f);
	/**
	 * 烘焙关卡俯视地图：把所有带碰撞的静态网格（含实例化网格）按最高点光栅化成高度图，
	 * 再做山体阴影和墙边描线，写成 Size×Size 的 PNG。上方 = 世界 +X（北），右方 = +Y。
	 * 返回参与绘制的组件数，0 表示失败。
	 */
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation", meta = (WorldContext = "WorldContext"))
	static int32 RenderLevelOverview(UObject* WorldContext, FVector2D Min, FVector2D Max, int32 Size,
	                                 const FString& File);
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static TArray<FVector> TraceTraversalSurface(UObject* WorldContext, FVector Start, FVector End);
	UFUNCTION(BlueprintCallable, Category="Traversal")
	static FString TraversalCapsuleBlocker(UObject* WorldContext, FVector Start, FVector End, float Radius,
	                                       float HalfHeight);
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
	/** Dump every anim graph node with its pins and links; Python cannot read anim graphs. */
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static FString DescribeAnimGraphChain(UAnimBlueprint* Blueprint);
	/** Bone list with parent indices, used to pick a layered blend branch root. */
	UFUNCTION(BlueprintCallable, Category = "Stinger|Validation")
	static FString DescribeSkeletonBones(class USkeleton* Skeleton);
	/** Retarget an existing montage onto another slot without reimporting its sequence. */
	UFUNCTION(BlueprintCallable, Category = "Stinger|Setup")
	static bool SetMontageSlotName(UAnimMontage* Montage, FName SlotName);
	/** Install <Slot> plus a layered blend per bone right before the output pose.
	 *  Melee attacks must not freeze the legs, and the weapon layer is inactive while the knife is out. */
	UFUNCTION(BlueprintCallable, Category = "Stinger|Setup")
	static bool ConfigureMeleeUpperBodySlot(UAnimBlueprint* Blueprint, FName SlotName, FName BranchBone);
};
