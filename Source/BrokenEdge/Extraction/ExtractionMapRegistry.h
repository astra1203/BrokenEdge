#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ExtractionMapDefinition.h"
#include "ExtractionMapRegistry.generated.h"

/** Server selection survives seamless travel; the registry contains only authored maps. */
UCLASS(Config=Game)
class BROKENEDGE_API UExtractionMapRegistry : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	UPROPERTY(Config) TArray<TSoftObjectPtr<UExtractionMapDefinition>> Maps;
	UPROPERTY(Transient) FName SelectedMap = TEXT("SpaceCenter");
	UExtractionMapDefinition* Find(FName Id) const;
	UExtractionMapDefinition* FindWorld(const UWorld* World) const;
	bool Validate(FName Id, FString& Error) const;
	static FString WorldPackage(const UWorld* World);
};
