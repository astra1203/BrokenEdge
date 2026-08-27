// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectileBullet.h"

#include "BrokenEdge/Character/BrokenEdgeCharacter.h"
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
}

void AProjectileBullet::OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
                              FVector NormalImpulse, const FHitResult& Hit)
{
	// Cast<ABrokenEdgeCharacter>(GetOwner())
	ABrokenEdgeCharacter* BrokenEdgeCharacter = Cast<ABrokenEdgeCharacter>(OtherActor);
	ABrokenEdgeCharacter* OwnerCharacter = Cast<ABrokenEdgeCharacter>(GetOwner());
	ABrokenEdgePlayerController* OwnerController = Cast<ABrokenEdgePlayerController>(OwnerCharacter->Controller);

	if (BrokenEdgeCharacter == nullptr || OwnerCharacter == nullptr||OwnerController == nullptr)return;
	
	if (OwnerCharacter->HasAuthority()&&!bUseServerSideRewind)
	{
		const float DamageToCause = Hit.BoneName.ToString() == FString("head") ? HeadShotDamage : Damage;
		UGameplayStatics::ApplyDamage(OtherActor, DamageToCause, OwnerController, 
			this, UDamageType::StaticClass());
		Super::OnHit(HitComp, OtherActor, OtherComp, NormalImpulse, Hit);
		return;
	}
	
	//@TODO
	
	


	Super::OnHit(HitComp, OtherActor, OtherComp, NormalImpulse, Hit);
}
