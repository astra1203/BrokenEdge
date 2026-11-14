#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Extraction/Persistence/ExtractionPersistence.h"
#include "Engine/GameInstance.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformProcess.h"
#include "JsonObjectConverter.h"

namespace
{
	UExtractionPersistence* Store(const FString& Path)
	{
		auto* S = NewObject<UExtractionPersistence>(NewObject<UGameInstance>());
		S->LoadForTest(Path);
		return S;
	}

	FString Directory()
	{
		return FPaths::ProjectSavedDir() / TEXT("Extraction/PersistenceTests") / FGuid::NewGuid().ToString();
	}

	bool Equip(UExtractionPersistence* S, FGuid P, FName Definition, int32 Slot = -1)
	{
		const auto* V = S->View(P);
		const auto* I = V->Stash.Items.FindByPredicate([&](const auto& Item) { return Item.Definition == Definition; });
		if (!I) return false;
		FExtractionStorageRequest R;
		R.Operation = FGuid::NewGuid();
		R.Revision = V->Revision;
		R.Move.Item = I->Id;
		R.Move.Quantity = I->Quantity;
		R.Move.Slot = Slot;
		return S->Move(P, R);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionStoreTransactions, "BrokenEdge.Extraction.Persistence.Transactions",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionStoreTransactions::RunTest(const FString&)
{
	const FString Path = Directory();
	auto* S = Store(Path);
	FGuid Id;
	FString Token;
	TestTrue(TEXT("Create durable profile"), S->OpenProfile({}, {}, Id, Token));
	TestEqual(TEXT("Three rifles only at creation"), S->View(Id)->Stash.Count(TEXT("AK")), 3);
	TestEqual(TEXT("Starter reserve"), S->View(Id)->Stash.Count(TEXT("Ammo")), 360);
	TestEqual(TEXT("Starter medicine"), S->View(Id)->Stash.Count(TEXT("Medkit")), 6);
	FExtractionStorageRequest R;
	R.Operation = FGuid::NewGuid();
	R.Revision = S->View(Id)->Revision;
	R.Move.Item = S->View(Id)->Stash.Items[0].Id;
	R.Move.Slot = 0;
	TestTrue(TEXT("Equip stored gun"), S->Move(Id, R));
	TestTrue(TEXT("Duplicate request succeeds without mutation"), S->Move(Id, R));
	TestEqual(TEXT("Only one gun removed"), S->View(Id)->Stash.Count(TEXT("AK")), 2);
	R.Operation = FGuid::NewGuid();
	TestFalse(TEXT("Stale revision rejected"), S->Move(Id, R));
	S = Store(Path);
	FGuid Again;
	FString NoToken;
	TestTrue(TEXT("Reopen with credential"), S->OpenProfile(Id, Token, Again, NoToken));
	TestEqual(TEXT("Restart does not regrant starter"), S->View(Id)->Stash.Count(TEXT("AK")), 2);
	TestTrue(TEXT("Gun GUID survives restart"), S->View(Id)->Loadout.InSlot(0)->Id == R.Move.Item);
	TestFalse(TEXT("Wrong credential rejected"), S->OpenProfile(Id, FString::ChrN(64, '0'), Again, NoToken));
	FGuid Raid;
	TestTrue(TEXT("Prepare raid"), S->BeginRaid({Id}, Raid));
	TestEqual(TEXT("Loadout deducted once"), S->View(Id)->Loadout.Items.Num(), 0);
	TestFalse(TEXT("Cannot launch twice"), S->BeginRaid({Id}, Raid));
	S = Store(Path);
	TestEqual(TEXT("Pre-start crash refunds"), S->View(Id)->Stash.Count(TEXT("AK")), 3);
	TestTrue(TEXT("Pre-start recovery result"), S->View(Id)->Result.Outcome == EExtractionOutcome::Aborted);
	TestTrue(TEXT("Empty loadout can enter raid"), S->BeginRaid({Id}, Raid));
	TestTrue(TEXT("Empty raid activation"), S->ActivateRaid(Raid));
	TestTrue(TEXT("Empty loadout extraction"), S->Settle(Raid, Id, EExtractionOutcome::Extracted,
	                                                     S->FindRaid(Raid)->Players[0].Original));
	S = Store(Path);
	TestEqual(TEXT("Empty round grants no new kit"), S->View(Id)->Stash.Count(TEXT("AK")), 3);
	S->BeginRaid({Id}, Raid);
	S->ActivateRaid(Raid);
	FExtractionContainer Full;
	Full.Width = Full.Height = 16;
	for (int32 N = 0; N < 125; ++N) ExtractionItems::AutoPlace(Full, ExtractionItems::Make(TEXT("Ammo"), 60));
	TestTrue(TEXT("Full stash settlement preserves overflow"), S->Settle(Raid, Id, EExtractionOutcome::Extracted,
	                                                                     Full));
	const auto* V = S->View(Id);
	TestTrue(TEXT("Overflow is nonempty"), V->Overflow.Num() > 0);
	int32 Total = V->Stash.Count(TEXT("Ammo"));
	for (const auto& I : V->Overflow) if (I.Definition == TEXT("Ammo")) Total += I.Quantity;
	TestEqual(TEXT("Stash plus overflow conserves every round"), Total, 360 + 125 * 60);
	R = FExtractionStorageRequest();
	R.Operation = FGuid::NewGuid();
	R.Revision = V->Revision;
	R.From = EExtractionStorageSide::Overflow;
	R.To = EExtractionStorageSide::Stash;
	R.Move.Item = V->Overflow[0].Id;
	R.Move.Quantity = V->Overflow[0].Quantity;
	TestFalse(TEXT("Full stash claim leaves overflow intact"), S->Move(Id, R));
	// 这局用的是合成背包，原出战背包没有带回；改为把一把步枪装进武器槽来腾出仓库空间（武器槽不需要网格）。
	TestTrue(TEXT("Free stash space"), Equip(S, Id, TEXT("AK"), 0));
	R.Revision = S->View(Id)->Revision;
	TestTrue(TEXT("Claim after space becomes available"), S->Move(Id, R));
	TestTrue(TEXT("Repeated claim is idempotent"), S->Move(Id, R));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionRecoveryProvenance, "BrokenEdge.Extraction.Persistence.RecoveryProvenance",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionRecoveryProvenance::RunTest(const FString&)
{
	const FString Path = Directory();
	auto* S = Store(Path);
	FGuid A, B;
	FString TokenA, TokenB;
	S->OpenProfile({}, {}, A, TokenA);
	S->OpenProfile({}, {}, B, TokenB);
	TestTrue(TEXT("A gun"), Equip(S, A, TEXT("AK"), 0));
	TestTrue(TEXT("B gun"), Equip(S, B, TEXT("AK"), 0));
	TestTrue(TEXT("A ammo"), Equip(S, A, TEXT("Ammo")));
	TestTrue(TEXT("B ammo"), Equip(S, B, TEXT("Ammo")));
	FGuid Raid;
	TestTrue(TEXT("Prepare both atomically"), S->BeginRaid({A, B}, Raid));
	TestTrue(TEXT("Durable active boundary"), S->ActivateRaid(Raid));
	const auto Record = *S->FindRaid(Raid);
	FExtractionContainer AInv = Record.Players[0].Original, BInv = Record.Players[1].Original;
	const FGuid Stolen = BInv.InSlot(0)->Id;
	FExtractionItemRequest Move;
	Move.Item = Stolen;
	Move.Slot = 1;
	FString Error;
	TestTrue(TEXT("Actual gun transfer keeps provenance"), ExtractionItems::Transfer(BInv, AInv, Move, Error));
	auto* AAmmo = AInv.Items.FindByPredicate([](const auto& I) { return I.Definition == TEXT("Ammo"); });
	auto* BAmmo = BInv.Items.FindByPredicate([](const auto& I) { return I.Definition == TEXT("Ammo"); });
	AAmmo->Quantity -= 20;
	ExtractionItems::TakeOrigins(AAmmo->Origins, 20);
	Move = FExtractionItemRequest();
	Move.Item = BAmmo->Id;
	Move.Quantity = 20;
	Move.MergeInto = AAmmo->Id;
	TestTrue(TEXT("Mixed stack tracks source units"), ExtractionItems::Transfer(BInv, AInv, Move, Error));
	BAmmo = BInv.Items.FindByPredicate([](const auto& I) { return I.Definition == TEXT("Ammo"); });
	auto* Gun = AInv.Find(AInv.InSlot(0)->Id);
	ExtractionItems::TakeOrigins(Gun->MagazineOrigins, 15);
	ExtractionItems::MergeOrigins(Gun->MagazineOrigins, ExtractionItems::TakeOrigins(BAmmo->Origins, 15));
	BAmmo->Quantity -= 15;
	TestTrue(TEXT("Extracted and exported sources commit together"), S->Settle(Raid, A, EExtractionOutcome::Extracted,
	                                                                           AInv));
	TestTrue(TEXT("Duplicate settlement is harmless"), S->Settle(Raid, A, EExtractionOutcome::Dead, AInv));
	S = Store(Path);
	TestEqual(TEXT("B cannot duplicate stolen gun"), S->View(B)->Stash.Count(TEXT("AK")), 2);
	TestEqual(TEXT("B only recovers 25 non-exported ammo units"), S->View(B)->Stash.Count(TEXT("Ammo")), 325);
	TestEqual(TEXT("A keeps both exported rifles"), S->View(A)->Stash.Count(TEXT("AK")), 4);
	TestTrue(TEXT("A success retained"), S->View(A)->Result.Outcome == EExtractionOutcome::Extracted);
	TestTrue(TEXT("B restored"), S->View(B)->Result.Outcome == EExtractionOutcome::Recovered);
	S = Store(Path);
	TestEqual(TEXT("Second restart cannot repeat refund"), S->View(B)->Stash.Count(TEXT("Ammo")), 325);
	TestTrue(TEXT("B can equip again"), Equip(S, B, TEXT("AK"), 0));
	S->BeginRaid({B}, Raid);
	S->ActivateRaid(Raid);
	TestTrue(TEXT("Death is committed"), S->Settle(Raid, B, EExtractionOutcome::Dead,
	                                               S->FindRaid(Raid)->Players[0].Original));
	S = Store(Path);
	TestEqual(TEXT("Confirmed death is never refunded"), S->View(B)->Stash.Count(TEXT("AK")), 1);
	// Export the original stack GUID after consuming part of it, then recover its remainder.
	// 背包随死亡丢失，只剩回到出战配置的安全箱；把这一堆弹药放进安全箱网格带入下一局。
	TestTrue(TEXT("Safe box returns to the loadout after death"),
	         S->View(B)->Loadout.InSlot(ExtractionGear::SafeSlot) != nullptr);
	TestTrue(TEXT("Equip original stack for GUID recovery"), Equip(S, B, TEXT("Ammo"), ExtractionGear::SafeGrid));
	const int32 Before = S->View(B)->Stash.Count(TEXT("Ammo"));
	S->BeginRaid({A, B}, Raid);
	S->ActivateRaid(Raid);
	const auto PartialRaid = *S->FindRaid(Raid);
	AInv = PartialRaid.Players[0].Original;
	BInv = PartialRaid.Players[1].Original;
	Move = FExtractionItemRequest();
	const auto* Stack = BInv.Items.FindByPredicate([](const auto& I) { return I.Definition == TEXT("Ammo"); });
	if (!TestNotNull(TEXT("Ammo stack entered the raid"), Stack)) return false;
	Move.Item = Stack->Id;
	Move.Quantity = Stack->Quantity;
	const int32 OriginalQuantity = Move.Quantity;
	TestTrue(TEXT("Transfer whole original stack"), ExtractionItems::Transfer(BInv, AInv, Move, Error));
	auto* Moved = AInv.Find(Move.Item);
	if (!TestNotNull(TEXT("Stack landed in A's backpack"), Moved)) return false;
	Moved->Quantity -= 10;
	ExtractionItems::TakeOrigins(Moved->Origins, 10);
	TestTrue(TEXT("Extract partially consumed original identity"), S->Settle(Raid, A, EExtractionOutcome::Extracted,
	                                                                         AInv));
	S = Store(Path);
	TestTrue(TEXT("Partial refund creates no duplicate item GUID"), S->IsAvailable());
	TestEqual(TEXT("Consumed but unexported units are restored"), S->View(B)->Stash.Count(TEXT("Ammo")), Before + 10);
	TestTrue(TEXT("Test actually exported some units"), OriginalQuantity > 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionJournalFailures, "BrokenEdge.Extraction.Persistence.JournalFailures",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionJournalFailures::RunTest(const FString&)
{
	const FString Path = Directory();
	auto* S = Store(Path);
	FGuid P;
	FString Token;
	S->OpenProfile({}, {}, P, Token);
	const int64 Sequence = S->TestState().Sequence;
	S->FailNextWrite = true;
	AddExpectedError(TEXT("EXTRACTION_STORE_WRITE_FAILED"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Write failure rejects mutation"), Equip(S, P, TEXT("AK"), 0));
	TestFalse(TEXT("Store blocks further operations"), S->IsAvailable());
	TestEqual(TEXT("In-memory state not committed"), S->TestState().Sequence, Sequence);
	auto& Files = FPlatformFileManager::Get().GetPlatformFile();
	{
		TUniquePtr<IFileHandle> File(Files.OpenWrite(*(Path / TEXT("Store.journal")), true));
		const uint32 Partial[] = {0x34455842, 1};
		File->Write(reinterpret_cast<const uint8*>(Partial), sizeof(Partial));
		File->Flush(true);
	}
	S = Store(Path);
	TestTrue(TEXT("Partial tail recovered"), S->IsAvailable());
	TestEqual(TEXT("Last completed transaction retained"), S->View(P)->Stash.Count(TEXT("AK")), 3);
	{
		TUniquePtr<IFileHandle> File(Files.OpenWrite(*(Path / TEXT("Store.journal")), true));
		File->Seek(0);
		const uint32 Corrupt = 0;
		File->Write(reinterpret_cast<const uint8*>(&Corrupt), sizeof(Corrupt));
		File->Flush(true);
	}
	S = Store(Path);
	TestFalse(TEXT("Committed corruption fails closed"), S->IsAvailable());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionPreparedCancellation,
                                 "BrokenEdge.Extraction.Persistence.PreparedCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionPreparedCancellation::RunTest(const FString&)
{
	const FString Path = Directory();
	auto* S = Store(Path);
	FGuid A, B, C, RaidA, RaidB, RaidC;
	FString Token;
	S->OpenProfile({}, {}, A, Token);
	S->OpenProfile({}, {}, B, Token);
	S->OpenProfile({}, {}, C, Token);
	Equip(S, A, TEXT("AK"), 0);
	Equip(S, B, TEXT("AK"), 0);
	Equip(S, C, TEXT("AK"), 0);
	S->BeginRaid({A}, RaidA);
	S->BeginRaid({B}, RaidB);
	S->BeginRaid({C}, RaidC);
	S->ActivateRaid(RaidC);
	TestTrue(TEXT("Cancel specified prepared raid"), S->AbortRaid(RaidA));
	TestTrue(TEXT("Only specified raid closes"), S->FindRaid(RaidA)->Closed && !S->FindRaid(RaidB)->Closed);
	TestEqual(TEXT("Specified original rifle refunded"), S->View(A)->Stash.Count(TEXT("AK")), 3);
	TestTrue(TEXT("Repeated targeted cancellation succeeds"), S->AbortRaid(RaidA));
	TestFalse(TEXT("Active raid cannot be aborted"), S->AbortRaid(RaidC));
	TestTrue(TEXT("Clear orphaned prepared records"), S->AbortPreparedRaids());
	TestTrue(TEXT("Prepared participant unlocked"), !S->IsInRaid(B));
	TestTrue(TEXT("Active participant remains locked"), S->IsInRaid(C));
	TestTrue(TEXT("Active outcome unchanged"), S->View(C)->Result.Outcome == EExtractionOutcome::None);
	TestEqual(TEXT("Prepared refund exactly once"), S->View(B)->Stash.Count(TEXT("AK")), 3);
	const int64 Sequence = S->TestState().Sequence;
	TestTrue(TEXT("Repeated sweep succeeds without mutation"), S->AbortPreparedRaids());
	TestEqual(TEXT("Repeated sweep does not append"), S->TestState().Sequence, Sequence);
	TestTrue(TEXT("Ready for another raid"), S->BeginRaid({A, B}, RaidA));
	const int64 BeforeFailure = S->TestState().Sequence;
	S->FailNextWrite = true;
	AddExpectedError(TEXT("EXTRACTION_STORE_WRITE_FAILED"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Failed refund cannot report success"), S->AbortPreparedRaids());
	TestEqual(TEXT("Failed refund leaves committed state intact"), S->TestState().Sequence, BeforeFailure);
	TestTrue(TEXT("Failed refund retains participant lock"), S->IsInRaid(A) && S->IsInRaid(B));
	TestFalse(TEXT("Failed refund blocks new starts"), S->IsAvailable());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionRecoverExistingStore, "BrokenEdge.Extraction.RecoverExistingStore",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionRecoverExistingStore::RunTest(const FString&)
{
	FString Path;
	if (!FParse::Value(FCommandLine::Get(), TEXT("ExtractionRecoveryStore="), Path))
	{
		AddError(TEXT("An explicit existing store directory is required"));
		return false;
	}
	if (!TestTrue(TEXT("Existing journal required"),
	              FPlatformFileManager::Get().GetPlatformFile().FileExists(*(Path / TEXT("Store.journal")))))
		return false;
	auto* S = Store(Path);
	if (!TestTrue(TEXT("Existing store loaded and recovered"), S->IsAvailable())) return false;
	for (const auto& Raid : S->TestState().Raids) TestTrue(TEXT("No orphaned record remains"), Raid.Closed);
	FString ProfileText, ItemText;
	if (FParse::Value(FCommandLine::Get(), TEXT("ExtractionVerifyProfile="), ProfileText))
	{
		FGuid Profile, Item;
		FGuid::Parse(ProfileText, Profile);
		FParse::Value(FCommandLine::Get(), TEXT("ExtractionVerifyItem="), ItemText);
		FGuid::Parse(ItemText, Item);
		const auto* View = S->View(Profile);
		if (TestNotNull(TEXT("Exporting profile survives process restart"), View))
		{
			TestTrue(TEXT("Extracted outcome survives restart"), View->Result.Outcome == EExtractionOutcome::Extracted);
			int32 Count = 0;
			for (const auto& I : View->Stash.Items) if (I.Id == Item) ++Count;
			TestEqual(TEXT("AI weapon instance exists exactly once after restart"), Count, 1);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_EXISTING_STORE_RECOVERED Profiles=%d Raids=%d"),
	       S->TestState().Profiles.Num(), S->TestState().Raids.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionCrashBoundary, "BrokenEdge.Extraction.CrashBoundary",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionCrashBoundary::RunTest(const FString&)
{
	FString Path, Boundary;
	if (!FParse::Value(FCommandLine::Get(), TEXT("ExtractionBoundaryDir="), Path)
		|| !FParse::Value(FCommandLine::Get(), TEXT("ExtractionBoundary="), Boundary))
		return true;
	auto* S = Store(Path);
	if (!TestTrue(TEXT("Store available"), S->IsAvailable())) return false;
	if (FParse::Param(FCommandLine::Get(), TEXT("ExtractionVerifyRecovery")))
	{
		if (!TestEqual(TEXT("Exactly one profile"), S->TestState().Profiles.Num(), 1)) return false;
		const auto& V = S->TestState().Profiles[0].View;
		TestEqual(TEXT("No duplicate or lost rifles after process restart"), V.Stash.Count(TEXT("AK")),
		          Boundary == TEXT("death") ? 2 : 3);
		TestEqual(TEXT("No starter regrant after restart"), V.Stash.Count(TEXT("Ammo")), 360);
		TestEqual(TEXT("Only the three reusable gear slots return to loadout"), V.Loadout.Items.Num(), 3);
		TestEqual(TEXT("Carried rifle is not duplicated into loadout"), V.Loadout.Count(TEXT("AK")), 0);
		if (!TestEqual(TEXT("One durable raid record"), S->TestState().Raids.Num(), 1)) return false;
		const auto& Record = S->TestState().Raids[0];
		TestTrue(TEXT("Recovery closes the durable raid"), Record.Closed);
		if (!TestEqual(TEXT("One durable raid member"), Record.Players.Num(), 1)) return false;
		for (int32 Slot : {ExtractionGear::RigSlot, ExtractionGear::BagSlot, ExtractionGear::SafeSlot})
		{
			const auto* Gear = V.Loadout.InSlot(Slot);
			const auto* Original = Record.Players[0].Original.InSlot(Slot);
			if (!TestNotNull(TEXT("Reusable gear restored"), Gear) || !TestNotNull(TEXT("Original gear recorded"), Original)) return false;
			TestEqual(TEXT("Gear definition preserved"), Gear->Definition, Original->Definition);
			TestEqual(TEXT("One item per gear slot"), Gear->Quantity, 1);
			if (Boundary != TEXT("death") || Slot == ExtractionGear::SafeSlot)
				TestEqual(TEXT("Refunded or protected gear keeps its identity"), Gear->Id, Original->Id);
			else TestTrue(TEXT("Lost rig and backpack get fresh starter identities"), Gear->Id != Original->Id);
		}
		TSet<FGuid> Identities;
		for (const auto* Items : {&V.Stash.Items, &V.Loadout.Items, &V.Overflow})
			for (const auto& Item : *Items)
			{
				TestTrue(TEXT("Recovered items have valid unique identities"), Item.Id.IsValid() && !Identities.Contains(Item.Id));
				Identities.Add(Item.Id);
			}
		const auto Expected = Boundary == TEXT("prepared")
			                      ? EExtractionOutcome::Aborted
			                      : Boundary == TEXT("active")
			                      ? EExtractionOutcome::Recovered
			                      : Boundary == TEXT("death")
			                      ? EExtractionOutcome::Dead
			                      : EExtractionOutcome::Extracted;
		TestTrue(TEXT("Committed terminal result or recovery result retained"), V.Result.Outcome == Expected);
		int32 Attempt = 1;
		FParse::Value(FCommandLine::Get(), TEXT("ExtractionRecoveryAttempt="), Attempt);
		FString Snapshot, Previous;
		if (!TestTrue(TEXT("Recovered profile serializes"), FJsonObjectConverter::UStructToJsonObjectString(V, Snapshot))) return false;
		const FString SnapshotPath = Path / TEXT("VerifiedProfile.json");
		if (Attempt == 1)
			TestTrue(TEXT("First independent recovery recorded"), FFileHelper::SaveStringToFile(Snapshot, *SnapshotPath));
		else
		{
			TestTrue(TEXT("First recovery snapshot exists"), FFileHelper::LoadFileToString(Previous, *SnapshotPath));
			TestEqual(TEXT("Second independent restart preserves complete profile and revisions"), Snapshot, Previous);
		}
		return true;
	}
	FGuid P, Raid;
	FString Token;
	if (!S->OpenProfile({}, {}, P, Token) || !Equip(S, P, TEXT("AK"), 0) || !S->BeginRaid({P}, Raid)) return false;
	if (Boundary != TEXT("prepared") && !S->ActivateRaid(Raid)) return false;
	if (Boundary == TEXT("success") || Boundary == TEXT("death"))
		if (!S->Settle(Raid, P, Boundary == TEXT("success") ? EExtractionOutcome::Extracted : EExtractionOutcome::Dead,
		               S->FindRaid(Raid)->Players[0].Original))
			return false;
	if (!FFileHelper::SaveStringToFile(TEXT("durable-boundary"), *(Path / TEXT("Ready.txt")))) return false;
	// The harness kills this owned process here; no orderly shutdown or in-process reload.
	for (;;) FPlatformProcess::Sleep(1);
}
#endif
