#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Extraction/ExtractionPersistence.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionJournalRolling, "BrokenEdge.Extraction.Persistence.JournalRolling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionJournalRolling::RunTest(const FString&)
{
    const FString Root = FPaths::ProjectSavedDir() / TEXT("Extraction/Rolling") / FGuid::NewGuid().ToString();
    auto& Files = FPlatformFileManager::Get().GetPlatformFile();
    auto NewStore = []() { return NewObject<UExtractionPersistence>(NewObject<UGameInstance>()); };
    auto Same = [](const auto& A, const auto& B)
    { return FExtractionStoreState::StaticStruct()->CompareScriptStruct(&A, &B, 0); };
    auto* Store = NewStore();
    TestTrue(TEXT("New isolated store"), Store->LoadForTest(Root));
    Store->JournalRollRecords = 2;
    Store->JournalRollBytes = 0;
    Store->JournalGenerations = 3;
    auto AddProfile = [&]()
    { FGuid Id; FString Credential; return Store->OpenProfile({}, {}, Id, Credential); };
    const FString Journal = Root / TEXT("Store.journal");
    for (int32 Index = 0; Index < 5; ++Index)
    {
        TestTrue(TEXT("Commit and roll"), AddProfile());
        auto* Reloaded = NewStore();
        TestTrue(TEXT("Read non-one segment start"), Reloaded->LoadForTest(Root));
        TestTrue(TEXT("Rolling preserves complete state"), Same(Store->TestState(), Reloaded->TestState()));
    }
    TArray<FString> Generations;
    IFileManager::Get().FindFiles(Generations, *(Journal + TEXT(".gen*")), true, false);
    TestEqual(TEXT("Keep configured archive count"), Generations.Num(), 3);
    for (int32 Step = 1; Step <= 3; ++Step)
    {
        Store->FailRollStepForTest = Step;
        AddExpectedError(TEXT("EXTRACTION_STORE_ROLL_FAILED"), EAutomationExpectedErrorFlags::Contains, 1);
        TestTrue(TEXT("Rolling failure does not reject durable commit"), AddProfile());
        TestTrue(TEXT("Rolling failure does not fault store"), Store->IsAvailable());
        TestFalse(TEXT("Failed roll cleans temporary segment"), Files.FileExists(*(Journal + TEXT(".next"))));
        auto* Reloaded = NewStore();
        TestTrue(TEXT("Restart after failed rolling step"), Reloaded->LoadForTest(Root));
        TestTrue(TEXT("Failed rolling preserves latest state"), Same(Store->TestState(), Reloaded->TestState()));
    }
    Store->FailRollStepForTest = 0;
    TestTrue(TEXT("Rolling resumes"), AddProfile());
    TestTrue(TEXT("Write flushed pending segment"), Store->WriteSegment(Journal + TEXT(".next")));
    auto* MainWins = NewStore();
    TestTrue(TEXT("Main journal wins over pending segment"), MainWins->LoadForTest(Root));
    TestFalse(TEXT("Pending segment removed when main exists"), Files.FileExists(*(Journal + TEXT(".next"))));
    TestTrue(TEXT("Rewrite pending for crash recovery"), Store->WriteSegment(Journal + TEXT(".next")));
    TestTrue(TEXT("Simulate missing active journal"), Files.DeleteFile(*Journal));
    auto* Promoted = NewStore();
    TestTrue(TEXT("Valid pending segment promoted"), Promoted->LoadForTest(Root));
    TestTrue(TEXT("Promotion preserves complete state"), Same(Store->TestState(), Promoted->TestState()));

    // Compatibility: a v1 snapshot at sequence one is still a legal replay root.
    const FString Legacy = Root / TEXT("Legacy");
    Files.CreateDirectoryTree(*Legacy);
    auto LegacyState = Store->TestState();
    LegacyState.Sequence = 1;
    FString Json;
    FJsonObjectConverter::UStructToJsonObjectString(LegacyState, Json, 0, 0, 0, nullptr, false);
    FTCHARToUTF8 Bytes(*Json);
    uint32 Header[] = {0x34455842, 1, static_cast<uint32>(Bytes.Length()), FCrc::MemCrc32(Bytes.Get(), Bytes.Length())};
    {
        TUniquePtr<IFileHandle> File(Files.OpenWrite(*(Legacy / TEXT("Store.journal"))));
        if (!TestTrue(TEXT("Create v1 fixture"), File.IsValid())) return false;
        TestTrue(TEXT("Write v1 header"), File->Write(reinterpret_cast<const uint8*>(Header), sizeof(Header)));
        TestTrue(TEXT("Write v1 payload"), File->Write(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length()));
        File->Flush(true);
    }
    auto* LegacyStore = NewStore();
    TestTrue(TEXT("Read v1 journal"), LegacyStore->LoadForTest(Legacy));
    TestTrue(TEXT("v1 state unchanged"), Same(LegacyState, LegacyStore->TestState()));

    TestTrue(TEXT("Prepare invalid pending case"), Store->WriteSegment(Journal + TEXT(".next")));
    {
        TUniquePtr<IFileHandle> File(Files.OpenWrite(*(Journal + TEXT(".next")), true));
        File->Truncate(9);
        File->Flush(true);
    }
    Files.DeleteFile(*Journal);
    auto* Invalid = NewStore();
    TestFalse(TEXT("Truncated pending never initializes empty store"), Invalid->LoadForTest(Root));
    TestFalse(TEXT("Invalid recovery faults store"), Invalid->IsAvailable());
    TestTrue(TEXT("Invalid evidence preserved"), Files.FileExists(*(Journal + TEXT(".next"))));
    return true;
}
#endif
