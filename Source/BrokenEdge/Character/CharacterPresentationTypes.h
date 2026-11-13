#pragma once
#include "CoreMinimal.h"
#include "CharacterPresentationTypes.generated.h"

USTRUCT(BlueprintType)
struct FStingerViewWeaponFit
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bEnabled = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform Hip = FTransform(FRotator(0, -90, 0), FVector(45, 12, -20));
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform Aim = FTransform(FRotator(0, -90, 0), FVector(45, 0, -8));
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform SourceRightHand;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform SourceLeftHand;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform SourceLeftMagazineOut;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform SourceLeftMagazineIn;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform MagazineGrip;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform MagazineReference;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName MagazineBone = TEXT("b_gun_mag");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1"))
	float MagazineOut = .12f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1"))
	float MagazineIn = .52f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.01", ClampMax="0.99"))
	float SwapAttachFraction = .55f;
};
