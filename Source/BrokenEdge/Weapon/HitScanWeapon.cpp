// Fill out your copyright notice in the Description page of Project Settings.


#include "HitScanWeapon.h"
#include "ExtractionBullet.h"
#include "BrokenEdge/Extraction/ExtractionCombatComponent.h"
#include "WeaponMechanics.h"
#include "BrokenEdge/Extraction/ExtractionState.h"
#include "BrokenEdge/Extraction/ExtractionHealth.h"

#include "BrokenEdge/BrokenEdge.h"
#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/LagCompensationComponent.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystemComponent.h"
#include "Sound/SoundCue.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarHitScanDebug(TEXT("BrokenEdge.Debug.HitScan"), 0,
                                                    TEXT("Draw short-lived hit scan impact markers."));

namespace
{
	const FName MuzzleSocketName(TEXT("MuzzleFlash"));

	USkeletalMeshComponent* GetLocalFirstPersonMuzzleMesh(const AWeapon* Weapon)
	{
		auto* Character = Cast<ABrokenEdgeCharacter>(Weapon->GetOwner());
		if (!Character || !Character->IsLocallyControlled() || !Character->IsFirstPersonView()
			|| Character->GetEquippedWeapon() != Weapon)
			return nullptr;

		auto* ViewMesh = Character->GetFirstPersonWeaponMesh();
		return IsValid(ViewMesh) && ViewMesh->IsRegistered() && ViewMesh->IsVisible()
		       && ViewMesh->DoesSocketExist(MuzzleSocketName)
			       ? ViewMesh
			       : nullptr;
	}
}


void AHitScanWeapon::Fire(const FVector& HitTarget)
{
	Super::Fire(HitTarget);

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerPawn == nullptr) return;

	const USkeletalMeshSocket* MuzzleFlashSocket = GetWeaponMesh()->GetSocketByName("MuzzleFlash");
	if (MuzzleFlashSocket == nullptr) return;

	FTransform SocketTransform = MuzzleFlashSocket->GetSocketTransform(GetWeaponMesh());
	FVector Start = SocketTransform.GetLocation();

	const auto* Mechanics = GetWorld()->GetGameState<AExtractionGameState>() ? WeaponMechanics::Find(ExtractionDefinition) : nullptr;
	if (Mechanics && (HasAuthority() || OwnerPawn->IsLocallyControlled()))
	{
		// A barrel that protrudes through cover cannot spawn a round beyond the wall.
		FCollisionQueryParams MuzzleQuery(SCENE_QUERY_STAT(ExtractionMuzzleObstruction), true, this);
		MuzzleQuery.AddIgnoredActor(OwnerPawn);
		FHitResult Obstruction;
		if (GetWorld()->LineTraceSingleByChannel(Obstruction, OwnerPawn->GetPawnViewLocation(), Start, ECC_Visibility, MuzzleQuery))
			Start = Obstruction.ImpactPoint + Obstruction.ImpactNormal * (Mechanics->Radius + .1f);
		FActorSpawnParameters Params;
		Params.Owner = this; Params.Instigator = OwnerPawn;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (auto* Bullet = GetWorld()->SpawnActor<AExtractionBullet>(AExtractionBullet::StaticClass(), Start, FRotator::ZeroRotator, Params))
			{
			const auto* C = Cast<ABrokenEdgeCharacter>(OwnerPawn);
			const auto& Effective = C ? C->GetExtractionCombat()->GetWeaponStats() : *Mechanics;
			Bullet->Initialize(Effective.IsValid() ? Effective : *Mechanics, HitTarget - Start, Damage, HeadShotDamage, ExtractionAmmoType, ImpactParticles, HitSound, BeamParticles);
			if (auto* ViewMesh = GetLocalFirstPersonMuzzleMesh(this))
				if (auto* PC = Cast<APlayerController>(OwnerPawn->GetController()); PC && PC->PlayerCameraManager)
					Bullet->SetVisualOrigin(UGameplayStatics::TransformWorldToFirstPerson(PC->PlayerCameraManager->GetCameraCacheView(),
						ViewMesh->GetSocketLocation(MuzzleSocketName), false));
		}
	}
	FHitResult FireHit;
	if (!Mechanics) WeaponTraceHit(Start, HitTarget, FireHit);

	ABrokenEdgeCharacter* HitCharacter = Cast<ABrokenEdgeCharacter>(FireHit.GetActor());
	AController* InstigatorController = OwnerPawn->GetController();

	if (HitCharacter && InstigatorController)
	{
		bool bCauseAuthDamage = !bUseServerSideRewind || OwnerPawn->IsLocallyControlled();
		if (HasAuthority() && bCauseAuthDamage) //服务端开火
		{
			const bool Head = GetWorld()->GetGameState<AExtractionGameState>()
				                  ? FireHit.BoneName == FName(TEXT("head"))
				                  : FireHit.BoneName.ToString() == FString("head");
			const float DamageToCause = Head ? HeadShotDamage : Damage;

			UE_LOG(LogTemp, Log, TEXT("Oreki [%llu][%s] Line:%d | %s Damage:%.2f"), GFrameCounter, TEXT(__FUNCTION__),
			       __LINE__,
			       TEXT(""), DamageToCause);

			PrintCollisionPresetSettings(HitCharacter->GetCapsuleComponent());
			PrintCollisionPresetSettings(HitCharacter->GetMesh());

			if (GetWorld()->GetGameState<AExtractionGameState>())
				HitCharacter->GetExtractionHealth()->Bullet(DamageToCause, FireHit.BoneName, ExtractionAmmoType,
				                                            InstigatorController, this);
			else
				UGameplayStatics::ApplyDamage(
					HitCharacter,
					DamageToCause,
					InstigatorController,
					this,
					UDamageType::StaticClass()
				);
		}

		if (!HasAuthority() && bUseServerSideRewind && !GetWorld()->GetGameState<AExtractionGameState>())
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
	if (!Mechanics && FireHit.bBlockingHit && ImpactParticles)
	{
		UGameplayStatics::SpawnEmitterAtLocation(
			GetWorld(),
			ImpactParticles,
			FireHit.ImpactPoint,
			FireHit.ImpactNormal.Rotation()
		);
	}
	if (!Mechanics && FireHit.bBlockingHit && HitSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			HitSound,
			FireHit.ImpactPoint
		);
	}

	if (MuzzleFlash)
	{
		if (auto* ViewMesh = GetLocalFirstPersonMuzzleMesh(this))
		{
			// Match the visible view weapon, including first-person FOV/scale and recoil.
			if (auto* Flash = UGameplayStatics::SpawnEmitterAttached(
				MuzzleFlash, ViewMesh, MuzzleSocketName, FVector::ZeroVector, FRotator::ZeroRotator,
				EAttachLocation::SnapToTarget, true, EPSCPoolMethod::None, false))
			{
				Flash->SetOnlyOwnerSee(true);
				Flash->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
				Flash->ActivateSystem();
			}
		}
		else
		{
			UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), MuzzleFlash, SocketTransform);
		}
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
	{
		DrawDebugSphere(GetWorld(), BeamEnd, 16.f, 12, FColor::Orange, false, 1.f);
		UE_LOG(LogTemp, Display,
		       TEXT("HITSCAN_TRACE Authority=%d Start=%s Target=%s Hit=%s Component=%s Bone=%s Impact=%s"),
		       HasAuthority(), *TraceStart.ToString(), *HitTarget.ToString(), *GetNameSafe(OutHit.GetActor()),
		       *GetNameSafe(OutHit.GetComponent()), *OutHit.BoneName.ToString(), *OutHit.ImpactPoint.ToString());
	}

	if (BeamParticles)
	{
		// Only the visual origin changes: tracing, damage and rewind use TraceStart above.
		FVector VisualStart = TraceStart;
		if (auto* ViewMesh = GetLocalFirstPersonMuzzleMesh(this))
		{
			auto* Pawn = Cast<APawn>(GetOwner());
			auto* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
			if (PC && PC->PlayerCameraManager)
			{
				// The beam remains in world space so its impact endpoint is not FOV-warped.
				VisualStart = UGameplayStatics::TransformWorldToFirstPerson(
					PC->PlayerCameraManager->GetCameraCacheView(), ViewMesh->GetSocketLocation(MuzzleSocketName),
					false);
			}
		}
		UParticleSystemComponent* Beam = UGameplayStatics::SpawnEmitterAtLocation(
			World,
			BeamParticles,
			VisualStart,
			FRotator::ZeroRotator,
			true
		);
		if (Beam)
		{
			Beam->SetVectorParameter(FName("Target"), BeamEnd);
		}
	}
}
