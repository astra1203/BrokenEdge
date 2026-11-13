#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ExtractionExitRegistry.generated.h"
class AExtractionExit;

/** World-local lifetime tracking includes replicated and streamed exit actors. */
UCLASS()
class BROKENEDGE_API UExtractionExitRegistry : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    void Register(AExtractionExit* Exit);
    void Unregister(AExtractionExit* Exit);
    const TArray<TWeakObjectPtr<AExtractionExit>>& GetExits() const { return Exits; }
private:
    TArray<TWeakObjectPtr<AExtractionExit>> Exits;
};
