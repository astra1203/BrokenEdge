#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ExtractionTraversalTypes.h"
#include "TraversalMovementComponent.generated.h"

struct FTraversalMoveResponse : FCharacterMoveResponseDataContainer
{
	FExtractionTraversalState State;
	virtual void ServerFillResponseData(const UCharacterMovementComponent& Movement,
	                                    const FClientAdjustment& Adjustment) override;
	virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map) override;
};

UCLASS()
class BROKENEDGE_API UTraversalMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UTraversalMovementComponent();
	bool Enabled() const;
	bool Active() const { return State.Kind != EExtractionTraversal::None; }
	bool OnLadder() const { return State.Ladder != nullptr && Active(); }
	bool CanTraverse() const;
	void RequestDetach(bool bJump);
	AExtractionLadder* FindLadder() const;
	UPROPERTY(Transient)
	FExtractionTraversalState State;
	FString LastRejection;
	bool bLadderInputBlocked = false, bDetachRequested = false, bJumpDetachRequested = false;
	bool bObstacleRequested = false;
	virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
	virtual void ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& Response) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void Finish(bool bJumpAway = false);
	bool ClearAt(const FVector& Position) const;
	bool ClearSegment(const FVector& From, const FVector& To) const;
	bool Standable(const FVector& Feet, FVector& Center) const;
	/**
	 * 攀爬/翻越的正面朝向：对齐障碍表面的水平法线，而不是直接沿用相机朝向。
	 * 斜着撞上箱体时若用相机朝向，角色会斜切过箱体边缘，动画朝向和障碍明显不符。
	 * 法线退化为竖直方向（表面是顶面/底面）时保持首选朝向。
	 */
	static FVector SnapFacingToNormal(const FVector& Preferred, const FVector& Normal);

private:
	FTraversalMoveResponse ResponseData;
	bool TryObstacle();
	bool TryLadder();
	AExtractionLadder* FindLadderEntry(bool& bTop, bool bRequireMovement) const;
	void UpdateLadderReentry();
	bool StartPath(FExtractionTraversalState Candidate);
	void Publish();
	void CancelActions();
	FVector PathPosition(float Fraction) const;
};
