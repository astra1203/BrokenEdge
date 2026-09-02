// Fill out your copyright notice in the Description page of Project Settings.


#include "Shotgun.h"

#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/LagCompensationComponent.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"


void AShotgun::FireShotgun(const TArray<FVector_NetQuantize>& HitTargets)
{
	AWeapon::Fire(FVector()); //播武器开火动画、消耗子弹
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerPawn == nullptr) return;

	const USkeletalMeshSocket* MuzzleFlashSocket = GetWeaponMesh()->GetSocketByName("MuzzleFlash");

	if (MuzzleFlashSocket)
	{
		const FVector Start = MuzzleFlashSocket->GetSocketTransform(GetWeaponMesh()).GetLocation();
		//map中用来存储被击中的角色和击中的弹丸数量
		TMap<ABrokenEdgeCharacter*, uint32> HitMap;
		TMap<ABrokenEdgeCharacter*, uint32> HeadShotHitMap;

		for (FVector_NetQuantize HitTarget : HitTargets)
		{
			FHitResult FireHit;
			WeaponTraceHit(Start, HitTarget, FireHit);

			if (ABrokenEdgeCharacter* HitCharacter = Cast<ABrokenEdgeCharacter>(FireHit.GetActor()))
			{
				const bool bHeadShot = FireHit.BoneName.ToString() == FString("head");

				if (bHeadShot)
				{
					// HeadShotHitMap[HitCharacter]++;

					if (HeadShotHitMap.Contains(HitCharacter)) HeadShotHitMap[HitCharacter]++;
					else HeadShotHitMap.Emplace(HitCharacter,1);

				}
				else
				{
					// HitMap[HitCharacter]++;
					if (HitMap.Contains(HitCharacter)) HitMap[HitCharacter]++;
					else HitMap.Emplace(HitCharacter,1);

				}

				//@TODO 粒子和音效
			}
		}

		TArray<ABrokenEdgeCharacter*> HitCharacters;

		//角色被击中的伤害计算
		TMap<ABrokenEdgeCharacter*, float> DamageMap;

		AController* InstigatorController = OwnerPawn->GetController();

		for (auto& HitPair : HitMap)
		{
			if (HitPair.Key)
			{
				DamageMap.Emplace(HitPair.Key, HitPair.Value * Damage);
				HitCharacters.AddUnique(HitPair.Key);
				// DamageMap[HitPair.Key] += Damage * HitPair.Value;
			}
		}

		for (auto& HeadShotHitPair : HeadShotHitMap)
		{
			if (HeadShotHitPair.Key)
			{
				if (DamageMap.Contains(HeadShotHitPair.Key)) DamageMap[HeadShotHitPair.Key] += HeadShotHitPair.Value * HeadShotDamage;
				else DamageMap.Emplace(HeadShotHitPair.Key, HeadShotHitPair.Value * HeadShotDamage);


				HitCharacters.AddUnique(HeadShotHitPair.Key);
				// DamageMap[HitPair.Key] += HeadShotDamage * HitPair.Value;


			}
		}

		bool bCauseAuthDamage = !bUseServerSideRewind || OwnerPawn->IsLocallyControlled();
		if (HasAuthority() && bCauseAuthDamage)
		{
			for (auto& DamagePair : DamageMap)
			{
				if (DamagePair.Key && InstigatorController)
				{
					UGameplayStatics::ApplyDamage(
						DamagePair.Key, //被击中的角色
						DamagePair.Value, //造成从上面循环得到的伤害之和
						InstigatorController,
						this,
						UDamageType::StaticClass()
					);
				}
			}
			return;
		}

		if (!HasAuthority() && bUseServerSideRewind)
		{
			BrokenEdgeOwnerCharacter = BrokenEdgeOwnerCharacter == nullptr
				                           ? Cast<ABrokenEdgeCharacter>(OwnerPawn)
				                           : BrokenEdgeOwnerCharacter;

			BrokenEdgeOwnerController = BrokenEdgeOwnerController == nullptr
				                            ? Cast<ABrokenEdgePlayerController>(InstigatorController)
				                            : BrokenEdgeOwnerController;

			if (BrokenEdgeOwnerCharacter && BrokenEdgeOwnerController &&
				BrokenEdgeOwnerCharacter->GetLagCompensation() && BrokenEdgeOwnerCharacter->IsLocallyControlled())
			{
				BrokenEdgeOwnerCharacter->GetLagCompensation()->ShotgunServerScoreRequest(
					HitCharacters,
					Start,
					HitTargets,
					BrokenEdgeOwnerController->GetServerTime() - BrokenEdgeOwnerController->SingleTripTime
				);
			}
		}
	}
}

void AShotgun::ShotgunTraceEndWithScatter(const FVector& HitTarget, TArray<FVector_NetQuantize>& HitTargets)
{
	const USkeletalMeshSocket* MuzzleFlashSocket = GetWeaponMesh()->GetSocketByName("MuzzleFlash");
	if (MuzzleFlashSocket == nullptr)return;

	const FVector TraceStart = MuzzleFlashSocket->GetSocketTransform(GetWeaponMesh()).GetLocation();

	const FVector ToTargetNormalized = (HitTarget - TraceStart).GetSafeNormal();
	const FVector SphereCenter = TraceStart + ToTargetNormalized * DistanceToSphere;

	for (uint32 i = 0; i < NumberOfPellets; i++)
	{
		const FVector RandVec = UKismetMathLibrary::RandomUnitVector() * FMath::FRandRange(0.f, SphereRadius);

		const FVector EndLoc = SphereCenter + RandVec;

		FVector ToEndLoc = EndLoc - TraceStart;

		ToEndLoc = TraceStart + ToEndLoc * TRACE_LENGTH / ToEndLoc.Size();

		HitTargets.Add(ToEndLoc);
	}
}
