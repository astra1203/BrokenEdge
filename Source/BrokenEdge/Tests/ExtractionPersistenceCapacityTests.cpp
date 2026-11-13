#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Extraction/ExtractionPersistence.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "JsonObjectConverter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionPersistenceCapacity, "BrokenEdge.Extraction.Persistence.Capacity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::StressFilter)

bool FExtractionPersistenceCapacity::RunTest(const FString&)
{
    const FString Root = FPaths::ProjectSavedDir() / TEXT("Extraction/Capacity") / FGuid::NewGuid().ToString();
    FString Report = TEXT("Profiles,ReceiptsPerProfile,ClosedRaids,RecordBytes,CommitMs,JournalBytes,LoadMs,ProcessPeakBytes\n");
    // Bounded initial tiers: measure near-limit behavior without committing an oversized record.
    const int32 Tiers[][3] = {{10, 100, 50}, {50, 1000, 500}, {100, 2000, 2000}};
    for (int32 Tier = 0; Tier < 3; ++Tier)
    {
        const FString Directory = Root / FString::FromInt(Tier);
        auto* Store = NewObject<UExtractionPersistence>(NewObject<UGameInstance>());
        Store->AddToRoot();
        if (!TestTrue(TEXT("Load isolated capacity store"), Store->LoadForTest(Directory)))
        { Store->RemoveFromRoot(); return false; }
        FGuid ProfileId;
        FString Credential;
        if (!TestTrue(TEXT("Seed valid profile"), Store->OpenProfile({}, {}, ProfileId, Credential)))
        { Store->RemoveFromRoot(); return false; }
        auto Candidate = Store->TestState();
        auto Template = Candidate.Profiles[0];
        Template.View.Stash.Items.Reset();
        Template.View.Loadout.Items.Reset();
        Candidate.Profiles.Reset();
        for (int32 Index = 0; Index < Tiers[Tier][0]; ++Index)
        {
            auto Profile = Template;
            Profile.View.Profile = FGuid::NewGuid();
            Profile.View.Stash.Id = FGuid::NewGuid();
            Profile.View.Loadout.Id = FGuid::NewGuid();
            for (int32 Receipt = 0; Receipt < Tiers[Tier][1]; ++Receipt)
                Profile.EconomyReceipts.Add({FGuid::NewGuid(), TEXT("capacity-retry-signature")});
            Candidate.Profiles.Add(MoveTemp(Profile));
        }
        for (int32 Index = 0; Index < Tiers[Tier][2]; ++Index)
        {
            FExtractionRaidRecord Raid;
            Raid.Id = FGuid::NewGuid();
            Raid.Closed = true;
            FExtractionRaidParticipant Participant;
            Participant.Profile = Candidate.Profiles[Index % Candidate.Profiles.Num()].View.Profile;
            Participant.Outcome = EExtractionOutcome::Extracted;
            Raid.Players.Add(Participant);
            Candidate.Raids.Add(MoveTemp(Raid));
        }
        const int64 BeforeBytes = IFileManager::Get().FileSize(*(Directory / TEXT("Store.journal")));
        double Started = FPlatformTime::Seconds();
        const bool Committed = Store->Commit(MoveTemp(Candidate));
        const double CommitMs = (FPlatformTime::Seconds() - Started) * 1000;
        if (!TestTrue(TEXT("Capacity commit"), Committed)) { Store->RemoveFromRoot(); return false; }
        const int64 JournalBytes = IFileManager::Get().FileSize(*(Directory / TEXT("Store.journal")));
        const int64 RecordBytes = JournalBytes - BeforeBytes - 5 * sizeof(uint32);
        auto* Reloaded = NewObject<UExtractionPersistence>(NewObject<UGameInstance>());
        Reloaded->AddToRoot();
        Started = FPlatformTime::Seconds();
        const bool Loaded = Reloaded->LoadForTest(Directory);
        const double LoadMs = (FPlatformTime::Seconds() - Started) * 1000;
        TestTrue(TEXT("Capacity replay"), Loaded);
        TestTrue(TEXT("Complete state survives replay"), FExtractionStoreState::StaticStruct()->CompareScriptStruct(
            &Store->TestState(), &Reloaded->TestState(), 0));
        Report += FString::Printf(TEXT("%d,%d,%d,%lld,%.2f,%lld,%.2f,%llu\n"),
            Tiers[Tier][0], Tiers[Tier][1], Tiers[Tier][2], RecordBytes, CommitMs, JournalBytes, LoadMs,
            static_cast<uint64>(FPlatformMemory::GetStats().PeakUsedPhysical));
        Store->RemoveFromRoot();
        Reloaded->RemoveFromRoot();
        CollectGarbage(RF_NoFlags);
    }
    const FString Path = Root / TEXT("Capacity.csv");
    TestTrue(TEXT("Save measured capacity report"), FFileHelper::SaveStringToFile(Report, *Path));
    AddInfo(FString::Printf(TEXT("Capacity report: %s"), *Path));
    return true;
}
#endif
