#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ExtractionItems.h"
#include "ExtractionPersistence.generated.h"

UENUM()
enum class EExtractionOutcome : uint8 { None, Extracted, Dead, Left, Disconnected, TimedOut, Recovered, Aborted };

USTRUCT()
struct FExtractionResult
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Raid;
	UPROPERTY()
	EExtractionOutcome Outcome = EExtractionOutcome::None;
	UPROPERTY()
	TArray<FExtractionItem> Items;
	UPROPERTY()
	int32 OverflowCount = 0;
};

USTRUCT()
struct FExtractionProfileView
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Store;
	UPROPERTY()
	FGuid Profile;
	UPROPERTY()
	int32 Revision = 0;
	UPROPERTY()
	FExtractionContainer Stash;
	UPROPERTY()
	FExtractionContainer Loadout;
	UPROPERTY()
	TArray<FExtractionItem> Overflow;
	UPROPERTY()
	FExtractionResult Result;
};

USTRUCT()
struct FExtractionStoredProfile
{
	GENERATED_BODY()
	UPROPERTY()
	FExtractionProfileView View;
	UPROPERTY()
	FString CredentialHash;
	UPROPERTY()
	FGuid LastOperation;
};

USTRUCT()
struct FExtractionRaidParticipant
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Profile;
	UPROPERTY()
	FExtractionContainer Original;
	UPROPERTY()
	EExtractionOutcome Outcome = EExtractionOutcome::None;
};

USTRUCT()
struct FExtractionRaidRecord
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Id;
	UPROPERTY()
	bool Active = false;
	UPROPERTY()
	bool Closed = false;
	UPROPERTY()
	TArray<FExtractionRaidParticipant> Players;
	UPROPERTY()
	TArray<FExtractionOrigin> Exported;
};

USTRUCT()
struct FExtractionStoreState
{
	GENERATED_BODY()
	UPROPERTY()
	int32 Version = 2;
	UPROPERTY()
	int64 Sequence = 0;
	UPROPERTY()
	FGuid Id;
	UPROPERTY()
	TArray<FExtractionStoredProfile> Profiles;
	UPROPERTY()
	TArray<FExtractionRaidRecord> Raids;
};

UENUM()
enum class EExtractionStorageSide : uint8 { Stash, Loadout, Overflow };

USTRUCT()
struct FExtractionStorageRequest
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Operation;
	UPROPERTY()
	int32 Revision = 0;
	UPROPERTY()
	EExtractionStorageSide From = EExtractionStorageSide::Stash;
	UPROPERTY()
	EExtractionStorageSide To = EExtractionStorageSide::Loadout;
	UPROPERTY()
	FExtractionItemRequest Move;
};

/** Single authoritative writer. A framed, flushed transaction contains the whole store,
 * including all affected profiles and raid export totals. No client-supplied item state. */
UCLASS()
class BROKENEDGE_API UExtractionPersistence : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	bool EnsureLoaded();
	bool OpenProfile(FGuid Requested, const FString& Credential, FGuid& Profile, FString& IssuedCredential);
	const FExtractionProfileView* View(FGuid Profile) const;
	bool Move(FGuid Profile, const FExtractionStorageRequest& Request);
	bool BeginRaid(const TArray<FGuid>& Profiles, FGuid& Raid);
	bool ActivateRaid(FGuid Raid);
	bool Settle(FGuid Raid, FGuid Profile, EExtractionOutcome Outcome, const FExtractionContainer& Inventory);
	bool AbortRaid(FGuid Raid);
	bool AbortPreparedRaids();
	const FExtractionRaidRecord* FindRaid(FGuid Raid) const;
	bool IsInRaid(FGuid Profile) const;
	FGuid StoreId() const { return State.Id; }
	bool IsAvailable() const { return Loaded && !Faulted; }
	FString Error;
	static FString OutcomeLabel(EExtractionOutcome Outcome);
#if WITH_DEV_AUTOMATION_TESTS
	friend class FExtractionDriver;
	bool LoadForTest(const FString& Directory);
	bool FailNextWrite = false;
	const FExtractionStoreState& TestState() const { return State; }
#endif

private:
	UPROPERTY()
	FExtractionStoreState State;
	FString Journal;
	bool Loaded = false;
	bool Faulted = false;
	bool Load(const FString& Directory);
	bool Commit(FExtractionStoreState Candidate);
	bool Recover();
	static void AbortPreparedRecord(FExtractionStoreState& Candidate, FExtractionRaidRecord& Raid);
	bool Validate(const FExtractionStoreState& Candidate) const;
	static void Deposit(FExtractionProfileView& Profile, TArray<FExtractionItem> Items);
};
