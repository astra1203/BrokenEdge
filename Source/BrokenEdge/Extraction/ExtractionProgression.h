#pragma once

#include "CoreMinimal.h"
#include "ExtractionItems.h"
#include "ExtractionProgression.generated.h"

UENUM()
enum class EExtractionEconomyAction : uint8 { Buy, Sell, AcceptQuest, DeliverQuest, ClaimQuest };

USTRUCT()
struct FExtractionOffer
{
	GENERATED_BODY()
	UPROPERTY() FName Definition;
	UPROPERTY() int32 Price = 0;
	UPROPERTY() int32 Level = 1;
};

USTRUCT()
struct FExtractionQuestDefinition
{
	GENERATED_BODY()
	UPROPERTY() FString Label;
	UPROPERTY() FString Description;
	UPROPERTY() int32 Target = 1;
	UPROPERTY() int32 Credits = 0;
	UPROPERTY() int32 Experience = 0;
};

/** All prototype economy tuning lives here; Game.ini may override these defaults. */
UCLASS(Config=Game, DefaultConfig)
class BROKENEDGE_API UExtractionProgressionRules : public UObject
{
	GENERATED_BODY()
public:
	UExtractionProgressionRules();
	UPROPERTY(Config) int32 StartingCredits = 2000;
	UPROPERTY(Config) TArray<int32> LevelExperience;
	UPROPERTY(Config) TArray<FExtractionOffer> Offers;
	UPROPERTY(Config) TArray<FExtractionQuestDefinition> Quests;
	UPROPERTY(Config) int32 ElectronicsSellPrice = 100;
	UPROPERTY(Config) int32 ToolsSellPrice = 200;
	const FExtractionOffer* Offer(FName Definition) const;
	int32 Level(int64 Experience) const;
	int64 SellPrice(const FExtractionItem& Item, int32 Quantity) const;
	bool IsValidConfiguration() const;
};

USTRUCT()
struct FExtractionQuestState
{
	GENERATED_BODY()
	UPROPERTY() bool Accepted = false;
	UPROPERTY() bool Claimed = false;
	UPROPERTY() int32 Progress = 0;
	UPROPERTY() bool Delivered = false;
	/** Source lots personally extracted after accepting the file quest. */
	UPROPERTY() TArray<FGuid> EligibleFiles;
};

USTRUCT()
struct FExtractionProgression
{
	GENERATED_BODY()
	UPROPERTY() int64 Credits = 0;
	UPROPERTY() int64 Experience = 0;
	UPROPERTY() TArray<FExtractionQuestState> Quests;
	void Initialize();
	bool Validate() const;
};

USTRUCT()
struct FExtractionRaidProgress
{
	GENERATED_BODY()
	UPROPERTY() int32 GuardKills = 0;
	/** Filled by authority from this raid's key-room recipe, never from a client. */
	UPROPERTY() TArray<FGuid> FileLots;
};

USTRUCT()
struct FExtractionEconomyRequest
{
	GENERATED_BODY()
	UPROPERTY() FGuid Operation;
	UPROPERTY() int32 Revision = 0;
	UPROPERTY() EExtractionEconomyAction Action = EExtractionEconomyAction::Buy;
	UPROPERTY() FName Definition;
	UPROPERTY() FGuid Item;
	UPROPERTY() int32 Quantity = 1;
	UPROPERTY() int32 Quest = INDEX_NONE;
	FString Signature() const;
};

USTRUCT()
struct FExtractionEconomyReceipt
{
	GENERATED_BODY()
	UPROPERTY() FGuid Operation;
	UPROPERTY() FString Signature;
};

namespace ExtractionProgression
{
	/** Mutates a candidate only; persistence commits money, items and receipts together. */
	bool Execute(FExtractionProgression& Progression, FExtractionContainer& Stash,
	             const FExtractionEconomyRequest& Request, FString& Error);
	void Extract(FExtractionProgression& Progression, const FExtractionContainer& Inventory,
	             const FExtractionRaidProgress& RaidProgress);
	FString Summary(const FExtractionProgression& Progression);
}
