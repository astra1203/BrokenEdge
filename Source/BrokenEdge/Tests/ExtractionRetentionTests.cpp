#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Extraction/Persistence/ExtractionPersistence.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Crc.h"
#include "JsonObjectConverter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionRetention, "BrokenEdge.Extraction.Persistence.Retention",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FExtractionRetention::RunTest(const FString&)
{
    const FString Root = FPaths::ProjectSavedDir() / TEXT("Extraction/Retention") / FGuid::NewGuid().ToString();
    auto NewStore = [] { return NewObject<UExtractionPersistence>(NewObject<UGameInstance>()); };
    auto* Store = NewStore();
    if (!TestTrue(TEXT("New store"), Store->LoadForTest(Root))) return false;
    FGuid Profile; FString Credential;
    if (!TestTrue(TEXT("Create profile"), Store->OpenProfile({}, {}, Profile, Credential))) return false;
    auto Candidate = Store->TestState();
    auto& P = Candidate.Profiles[0];
    P.View.Revision = 2000;
    TArray<FExtractionEconomyRequest> Requests;
    for (int32 N = 0; N < 1030; ++N)
    {
        FExtractionEconomyRequest R;
        R.Operation = FGuid::NewGuid(); R.Revision = N;
        Requests.Add(R); P.EconomyReceipts.Add({R.Operation, R.Signature()});
    }
    const FGuid ReferencedLot = FGuid::NewGuid();
    auto Item = ExtractionItems::Make(TEXT("Ammo"), 1);
    Item.Origins = {{ReferencedLot, 1}};
    TestTrue(TEXT("Seed retained provenance"), ExtractionItems::AutoPlace(P.View.Stash, Item));
    for (int32 N = 0; N < 132; ++N)
    {
        FExtractionRaidRecord R; R.Id = FGuid::NewGuid(); R.Closed = true;
        if (!N) R.Exported.Add({ReferencedLot, 1});
        Candidate.Raids.Add(R);
    }
    const FGuid Pinned = Candidate.Raids[0].Id, Removed = Candidate.Raids[1].Id;
    FExtractionRaidRecord Active; Active.Id = FGuid::NewGuid(); Active.Active = true;
    Candidate.Raids.Add(Active);
    // Write an actual v7 fixture, then exercise backup and migration through Load.
    auto Legacy = Candidate; Legacy.Version = 7; Legacy.Sequence = 1;
    FString Json; FJsonObjectConverter::UStructToJsonObjectString(Legacy, Json, 0, 0, 0, nullptr, false);
    FTCHARToUTF8 Bytes(*Json);
    uint32 Header[] = {0x34455842, 1, static_cast<uint32>(Bytes.Length()), FCrc::MemCrc32(Bytes.Get(), Bytes.Length())};
    TArray<uint8> Frame; Frame.Append(reinterpret_cast<const uint8*>(Header), sizeof(Header));
    Frame.Append(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
    const FString LegacyDir = Root / TEXT("Legacy"); IFileManager::Get().MakeDirectory(*LegacyDir, true);
    TestTrue(TEXT("Write legacy journal"), FFileHelper::SaveArrayToFile(Frame, *(LegacyDir / TEXT("Store.journal"))));
    auto* Migrated = NewStore();
    TestTrue(TEXT("Load and migrate v7"), Migrated->LoadForTest(LegacyDir));
    TestEqual(TEXT("Current version"), Migrated->TestState().Version, 9);
    TArray<uint8> Backup;
    TestTrue(TEXT("Read v7 backup"), FFileHelper::LoadFileToArray(Backup, *(LegacyDir / TEXT("Store.journal.v7.bak"))));
    TestTrue(TEXT("Backup preserves exact bytes"), Backup == Frame);

    TestTrue(TEXT("Commit retention"), Store->Commit(Candidate));
    TestEqual(TEXT("Receipt window"), Store->TestState().Profiles[0].EconomyReceipts.Num(), 1024);
    TestEqual(TEXT("128 recent plus pinned closed and active"), Store->TestState().Raids.Num(), 130);
    TestNotNull(TEXT("Referenced raid preserved"), Store->FindRaid(Pinned));
    TestNotNull(TEXT("Open raid preserved"), Store->FindRaid(Active.Id));
    TestNull(TEXT("Old unreferenced raid removed"), Store->FindRaid(Removed));
    TestTrue(TEXT("Retry inside window remains idempotent"), Store->Economy(Profile, Requests.Last()));
    TestFalse(TEXT("Pruned retry rejected by stale revision"), Store->Economy(Profile, Requests[0]));
    TestFalse(TEXT("Pruned raid cannot settle again"), Store->Settle(Removed, Profile, EExtractionOutcome::Extracted, {}));
    FGuid LiveRaid;
    TestTrue(TEXT("Prepare a real participant after pruning"), Store->BeginRaid({Profile}, LiveRaid));
    TestTrue(TEXT("Activate retained live raid"), Store->ActivateRaid(LiveRaid));
    TestTrue(TEXT("Live participant can still settle"), Store->Settle(LiveRaid, Profile, EExtractionOutcome::Dead, {}));
    auto* Reloaded = NewStore();
    TestTrue(TEXT("Retained state restarts and recovers open records"), Reloaded->LoadForTest(Root));
    TestNotNull(TEXT("Pinned provenance survives recovery"), Reloaded->FindRaid(Pinned));
    TestEqual(TEXT("Receipt bound survives restart"), Reloaded->TestState().Profiles[0].EconomyReceipts.Num(), 1024);
    return true;
}
#endif
