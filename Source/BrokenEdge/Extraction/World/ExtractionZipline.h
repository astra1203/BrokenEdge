#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionZipline.generated.h"
// 滑索（项目自定）：两根立柱之间一条钢索，站在任一端按 F 挂上，沿钢索滑到另一端。
// 只负责外观、可达判定和起止点；骑乘移动由角色（ABrokenEdgeCharacter::StartZipline）完成，
// 本地操控端每帧沿索输入方向，服务器按同样的移动输入模拟，所以联机下和普通移动一样有预测。

class UStaticMeshComponent;
class ABrokenEdgeCharacter;

UCLASS()
class BROKENEDGE_API AExtractionZipline : public AActor
{
	GENERATED_BODY()

public:
	AExtractionZipline();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	/** 两端挂索点（相对 Actor，厘米），立柱从挂索点往下 PostHeight。 */
	UPROPERTY(EditAnywhere, Category="Zipline", meta=(MakeEditWidget))
	FVector TopAnchor = FVector(0, 0, 350);
	UPROPERTY(EditAnywhere, Category="Zipline", meta=(MakeEditWidget))
	FVector BottomAnchor = FVector(1500, 0, -900);
	UPROPERTY(EditAnywhere, Category="Zipline")
	float PostHeight = 350.f;
	/** 两端在提示里的名字，例如“池边”“池底”。 */
	UPROPERTY(EditAnywhere, Category="Zipline")
	FText TopLabel;
	UPROPERTY(EditAnywhere, Category="Zipline")
	FText BottomLabel;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> TopPost;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> BottomPost;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Cable;

	/** 骑乘时角色胶囊中心比钢索低多少（手挂在索上）。 */
	static constexpr float HangDrop = 125.f;

	FVector TopWorld() const { return GetActorTransform().TransformPosition(TopAnchor); }
	FVector BottomWorld() const { return GetActorTransform().TransformPosition(BottomAnchor); }
	// 站在某一端立柱旁（2 米内、高度相近）才可挂索；bFromTop 返回在哪一端。
	bool CanReach(const ABrokenEdgeCharacter* Character, bool& bFromTop) const;
	FString PromptText(const ABrokenEdgeCharacter* Character) const;
	// 服务器：再次校验后让角色开始滑；成功返回空串，失败返回提示。
	FString Use(ABrokenEdgeCharacter* Character);

private:
	void ApplyLayout();
};
