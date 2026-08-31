// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectileBullet.h"

#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
#include "BrokenEdge/Component/LagCompensationComponent.h"
#include "BrokenEdge/PlayerController/BrokenEdgePlayerController.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"



AProjectileBullet::AProjectileBullet()
{
	ProjectileMovementComponent = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovementComponent"));
	
	ProjectileMovementComponent->bRotationFollowsVelocity = true;
	ProjectileMovementComponent->SetIsReplicated(true);
	ProjectileMovementComponent->InitialSpeed =InitialSpeed;
	ProjectileMovementComponent->MaxSpeed = InitialSpeed;
	
	ProjectileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileMesh"));
	ProjectileMesh->SetupAttachment(RootComponent);
}

void AProjectileBullet::OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
                              FVector NormalImpulse, const FHitResult& Hit)
{
	// Cast<ABrokenEdgeCharacter>(GetOwner())
	ABrokenEdgeCharacter* HitCharacter = Cast<ABrokenEdgeCharacter>(OtherActor);
	ABrokenEdgeCharacter* OwnerCharacter = Cast<ABrokenEdgeCharacter>(GetOwner());
	ABrokenEdgePlayerController* OwnerController = Cast<ABrokenEdgePlayerController>(OwnerCharacter->Controller);

	if (HitCharacter == nullptr || OwnerCharacter == nullptr||OwnerController == nullptr)
	{
		Destroy();
		return;
	}
		
	if (OwnerCharacter->HasAuthority()&&!bUseServerSideRewind)
	{
		const float DamageToCause = Hit.BoneName.ToString() == FString("head") ? HeadShotDamage : Damage;
		UGameplayStatics::ApplyDamage(OtherActor, DamageToCause, OwnerController, 
			this, UDamageType::StaticClass());
		Super::OnHit(HitComp, OtherActor, OtherComp, NormalImpulse, Hit);
		return;
	}
	
	if (bUseServerSideRewind&&OwnerCharacter->GetLagCompensation()&&
		OwnerCharacter->IsLocallyControlled() && HitCharacter)
	{
		OwnerCharacter->GetLagCompensation()->ProjectileServerScoreRequest(
		HitCharacter,
		TraceStart,
		InitialVelocity,
		OwnerController->GetServerTime()-OwnerController->SingleTripTime
		);
		
	}
	
	Super::OnHit(HitComp, OtherActor, OtherComp, NormalImpulse, Hit);
}
