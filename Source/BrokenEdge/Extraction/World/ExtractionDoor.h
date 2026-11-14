#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionDoor.generated.h"

/** Ordinary hinged door: no keys, inventory mutation or loot spawning. */
UCLASS()
class BROKENEDGE_API AExtractionDoor : public AActor
{
	GENERATED_BODY()
public:
	AExtractionDoor();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Door;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float OpenAngle = 95.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float MoveSeconds = .8f;
	UPROPERTY(EditAnywhere) TObjectPtr<class USoundBase> OpenSound;
	UPROPERTY(EditAnywhere) TObjectPtr<class USoundBase> CloseSound;
	UPROPERTY(ReplicatedUsing=OnRep_Motion, BlueprintReadOnly) bool bOpen = false;
	UPROPERTY(ReplicatedUsing=OnRep_Motion) double MotionStartedAt = -1;
	bool CanReach(const class ABrokenEdgeCharacter* Character) const;
	FString Toggle(class ABrokenEdgeCharacter* Character);
	FVector InteractionPoint() const;
private:
	UFUNCTION() void OnRep_Motion();
	void ApplyMotion();
	bool SweepOccupied() const;
	double Now() const;
	FRotator ClosedRotation;
	double LastSoundAt = -1;
};
