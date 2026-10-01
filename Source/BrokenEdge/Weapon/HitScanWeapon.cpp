// Fill out your copyright notice in the Description page of Project Settings.


#include "HitScanWeapon.h"

#include "BrokenEdge/BrokenEdge.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/LagCompensationComponent.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystemComponent.h"
#include "Sound/SoundCue.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarHitScanDebug(TEXT("BrokenEdge.Debug.HitScan"), 0,
                                                    TEXT("Draw short-lived hit scan impact markers."));


void AHitScanWeapon::Fire(const FVector& HitTarget)
{
	Super::Fire(HitTarget);

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerPawn == nullptr) return;

	const USkeletalMeshSocket* MuzzleFlashSocket = GetWeaponMesh()->GetSocketByName("MuzzleFlash");
	if (MuzzleFlashSocket == nullptr) return;

	FTransform SocketTransform = MuzzleFlashSocket->GetSocketTransform(GetWeaponMesh());
	FVector Start = SocketTransform.GetLocation();

	FHitResult FireHit;
	WeaponTraceHit(Start, HitTarget, FireHit);

	ABrokenEdgeCharacter* HitCharacter = Cast<ABrokenEdgeCharacter>(FireHit.GetActor());
	AController* InstigatorController = OwnerPawn->GetController();

	if (HitCharacter && InstigatorController)
	{
		bool bCauseAuthDamage = !bUseServerSideRewind || OwnerPawn->IsLocallyControlled();
		if (HasAuthority() && bCauseAuthDamage) //服务端开火
		{
			const float DamageToCause = FireHit.BoneName.ToString() == FString("head") ? HeadShotDamage : Damage;

			UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s Damage:%.2f"), GFrameCounter, TEXT(__FUNCTION__),
			       __LINE__,
			       TEXT(""), DamageToCause);

			PrintCollisionPresetSettings(HitCharacter->GetCapsuleComponent());
			PrintCollisionPresetSettings(HitCharacter->GetMesh());

			UGameplayStatics::ApplyDamage(
				HitCharacter,
				DamageToCause,
				InstigatorController,
				this,
				UDamageType::StaticClass()
			);
		}

		if (!HasAuthority() && bUseServerSideRewind)
		{
			BrokenEdgeOwnerCharacter = BrokenEdgeOwnerCharacter == nullptr
				                           ? Cast<ABrokenEdgeCharacter>(OwnerPawn)
				                           : BrokenEdgeOwnerCharacter;
			BrokenEdgeOwnerController = BrokenEdgeOwnerController == nullptr
				                            ? Cast<ABrokenEdgePlayerController>(InstigatorController)
				                            : BrokenEdgeOwnerController;

			if (BrokenEdgeOwnerController && BrokenEdgeOwnerCharacter &&
				BrokenEdgeOwnerCharacter->GetLagCompensation() && BrokenEdgeOwnerCharacter->IsLocallyControlled())
			{
				BrokenEdgeOwnerCharacter->GetLagCompensation()->HitScanServerScoreRequest(
					HitCharacter,
					Start,
					HitTarget,
					BrokenEdgeOwnerController->GetServerTime() - BrokenEdgeOwnerController->SingleTripTime
				);
			}
		}
	}
	if (ImpactParticles)
	{
		UGameplayStatics::SpawnEmitterAtLocation(
			GetWorld(),
			ImpactParticles,
			FireHit.ImpactPoint,
			FireHit.ImpactNormal.Rotation()
		);
	}
	if (HitSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			HitSound,
			FireHit.ImpactPoint
		);
	}

	if (MuzzleFlash)
	{
		UGameplayStatics::SpawnEmitterAtLocation(
			GetWorld(),
			MuzzleFlash,
			SocketTransform
		);
	}
	if (FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			FireSound,
			GetActorLocation()
		);
	}
}

void AHitScanWeapon::WeaponTraceHit(const FVector& TraceStart, const FVector& HitTarget, FHitResult& OutHit)
{
	UWorld* World = GetWorld();
	if (World == nullptr) return;

	FVector End = TraceStart + (HitTarget - TraceStart) * 1.25f;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(WeaponTrace), false, this);
	QueryParams.AddIgnoredActor(GetOwner());

	World->LineTraceSingleByChannel(
		OutHit,
		TraceStart,
		End,
		ECollisionChannel::ECC_Visibility,
		QueryParams
	);

	FVector BeamEnd = End;
	if (OutHit.bBlockingHit)
	{
		BeamEnd = OutHit.ImpactPoint;
	}
	else
	{
		OutHit.ImpactPoint = End;
	}

	if (CVarHitScanDebug.GetValueOnGameThread())
		DrawDebugSphere(GetWorld(), BeamEnd, 16.f, 12, FColor::Orange, false, 1.f);

	if (BeamParticles)
	{
		UParticleSystemComponent* Beam = UGameplayStatics::SpawnEmitterAtLocation(
			World,
			BeamParticles,
			TraceStart,
			FRotator::ZeroRotator,
			true
		);
		if (Beam)
		{
			Beam->SetVectorParameter(FName("Target"), BeamEnd);
		}
	}
}
