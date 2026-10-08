#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TraversalAcceptanceFixture.generated.h"

/** Only spawned by the Development automation driver; never placed in production maps. */
UCLASS()
class ATraversalAcceptanceFixture : public AActor
{
	GENERATED_BODY()

public:
	ATraversalAcceptanceFixture();
	virtual void BeginPlay() override;
};
