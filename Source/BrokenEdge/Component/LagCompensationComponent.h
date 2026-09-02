// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LagCompensationComponent.generated.h"


/**
 * 碰撞盒信息结构，存储碰撞盒的位置、旋转和尺寸
 */
USTRUCT(BlueprintType)
struct FBoxInformation
{
	GENERATED_BODY()

	/** 碰撞盒位置 */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** 碰撞盒旋转 */
	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;

	/** 碰撞盒尺寸 */
	UPROPERTY()
	FVector BoxExtent = FVector::ZeroVector;
};

/**
 * 帧数据包结构，存储某一时刻角色的碰撞盒信息
 */
USTRUCT(BlueprintType)
struct FFramePackage
{
	GENERATED_BODY()

	/** 帧时间 */
	UPROPERTY()
	double Time = 0.f;

	/** 碰撞盒信息映射 */
	UPROPERTY()
	TMap<FName, FBoxInformation> HitBoxInfo;

	/** 关联的角色 */
	UPROPERTY()
	class ABrokenEdgeCharacter* Character = nullptr;
};

/**
 * 服务器端倒带结果结构
 */
USTRUCT(BlueprintType)
struct FServerSideRewindResult
{
	GENERATED_BODY()

	/** 是否确认命中 */
	UPROPERTY()
	bool bHitConfirmed = false;

	/** 是否为爆头 */
	UPROPERTY()
	bool bHeadShot = false;
};

/**
 * 霰弹枪服务器端倒带结果结构
 */
USTRUCT(BlueprintType)
struct FShotgunServerSideRewindResult
{
	GENERATED_BODY()

	/** 爆头结果映射 */
	UPROPERTY()
	TMap<ABrokenEdgeCharacter*, uint32> HeadShots;

	/** 身体命中结果映射 */
	UPROPERTY()
	TMap<ABrokenEdgeCharacter*, uint32> BodyShots;
};


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BROKENEDGE_API ULagCompensationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	ULagCompensationComponent();

	friend class ABrokenEdgeCharacter;

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	/**
 * 获取需要检测的帧
 * @param HitCharacter 目标角色
 * @param HitTime 命中时间
 * @return 帧数据包
 */
	FFramePackage GetFrameToCheck(ABrokenEdgeCharacter* HitCharacter, float HitTime);

	/**
 * 在两帧之间插值
 * @param OlderFrame 旧帧
 * @param YoungerFrame 新帧
 * @param HitTime 命中时间
 * @return 插值后的帧数据包
 */
	FFramePackage InterpBetweenFrames(const FFramePackage& OlderFrame, const FFramePackage& YoungerFrame,
	                                  float HitTime);

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	void SaveFramePackageServer();
	void SaveFramePackage(FFramePackage& Package);

	/**
 * @brief 执行服务器端倒带（投射物武器）
 * @param HitCharacter 被命中的角色
 * @param TraceStart 投射物起点
 * @param InitialVelocity 初始速度
 * @param HitTime 命中时间
 * @return 命中确认结果
 */
	FServerSideRewindResult ProjectileServerSideRewind(ABrokenEdgeCharacter* HitCharacter,
	                                                   const FVector_NetQuantize& TraceStart,
	                                                   const FVector_NetQuantize100& InitialVelocity, float HitTime);

	/**
 * 服务器分数请求（射弹武器）
 * @param HitCharacter 命中角色
 * @param TraceStart 射线起点
 * @param InitialVelocity 初始速度
 * @param HitTime 命中时间
 */
	UFUNCTION(Server, Reliable)
	void ProjectileServerScoreRequest(ABrokenEdgeCharacter* HitCharacter, const FVector_NetQuantize& TraceStart,
	                                  const FVector_NetQuantize100& InitialVelocity, float HitTime);


	FServerSideRewindResult ProjectileConfirmHit(const FFramePackage& FramePackage, ABrokenEdgeCharacter* HitCharacter,
	                                             const FVector_NetQuantize& TraceStart,
	                                             const FVector_NetQuantize100& InitialVelocity, float HitTime);

	void CacheBoxPositions(ABrokenEdgeCharacter* HitCharacter, FFramePackage& OutFramePackage);
	void MoveBoxes(ABrokenEdgeCharacter* HitCharacter, const FFramePackage& FramePackage);
	void EnableCharacterMeshCollision(ABrokenEdgeCharacter* HitCharacter, ECollisionEnabled::Type CollisionEnabled);
	void ResetHitBoxes(ABrokenEdgeCharacter* HitCharacter, const FFramePackage& Package);


	/**
 * 服务器分数请求（扫描武器）
 * @param HitCharacter 命中角色
 * @param TraceStart 射线起点
 * @param HitLocation 命中位置
 * @param HitTime 命中时间
 */
	UFUNCTION(Server, Reliable)
	void HitScanServerScoreRequest(ABrokenEdgeCharacter* HitCharacter, const FVector_NetQuantize& TraceStart,
	                               const FVector_NetQuantize& HitLocation, float HitTime);

	FServerSideRewindResult HitScanServerSideRewind(ABrokenEdgeCharacter* HitCharacter,
	                                                const FVector_NetQuantize& TraceStart,
	                                                const FVector_NetQuantize& HitLocation, float HitTime);

	FServerSideRewindResult HitScanConfirmHit(const FFramePackage& FramePackage, ABrokenEdgeCharacter* HitCharacter,
	                                          const FVector_NetQuantize& TraceStart,
	                                          const FVector_NetQuantize& HitLocation);

	/**
 * 服务器分数请求（霰弹枪）
 * @param HitCharacters 命中角色列表
 * @param TraceStart 射线起点
 * @param HitLocations 命中位置列表
 * @param HitTime 命中时间
 */
	UFUNCTION(Server, Reliable)
	void ShotgunServerScoreRequest(const TArray<ABrokenEdgeCharacter*>& HitCharacters,
	                               const FVector& TraceStart, const TArray<FVector_NetQuantize>& HitLocations,
	                               float HitTime);

	FShotgunServerSideRewindResult ShotgunServerSideRewind(const TArray<ABrokenEdgeCharacter*>& HitCharacters,
	                                                       const FVector& TraceStart,
	                                                       const TArray<FVector_NetQuantize>& HitLocations,
	                                                       float HitTime);

	FShotgunServerSideRewindResult ShotgunConfirmHit(const TArray<FFramePackage>& FramePackages,
	                                                 const FVector& TraceStart,
	                                                 const TArray<FVector_NetQuantize>& HitLocations);

private:
	/** 关联的角色 */
	UPROPERTY()
	class ABrokenEdgeCharacter* Character = nullptr;

	/** 关联的玩家控制器 */
	UPROPERTY()
	class ABrokenEdgePlayerController* Controller = nullptr;

	/** 帧历史记录 */
	TDoubleLinkedList<FFramePackage> FrameHistory;

	/** 最大记录时间 */
	UPROPERTY(EditAnywhere)
	float MaxRecordTime = 4.f;


};
