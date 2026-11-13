#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Extraction/ExtractionPersistence.h"
#include "Engine/GameInstance.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Crc.h"
#include "HAL/PlatformFileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformProcess.h"

namespace
{
	FString ProgressionDirectory()
	{
		return FPaths::ProjectSavedDir() / TEXT("Extraction/ProgressionTests") / FGuid::NewGuid().ToString();
	}

	UExtractionPersistence* ProgressionStore(const FString& Directory)
	{
		auto* Store = NewObject<UExtractionPersistence>(NewObject<UGameInstance>());
		Store->LoadForTest(Directory);
		return Store;
	}

	FExtractionEconomyRequest EconomyRequest(const UExtractionPersistence* Store, FGuid Profile,
	                                         EExtractionEconomyAction Action, FName Definition = NAME_None,
	                                         int32 Quantity = 1, int32 Quest = INDEX_NONE)
	{
		FExtractionEconomyRequest R;
		R.Operation = FGuid::NewGuid();
		R.Revision = Store->View(Profile)->Revision;
		R.Action = Action;
		R.Definition = Definition;
		R.Quantity = Quantity;
		R.Quest = Quest;
		return R;
	}

	bool QuestOperation(UExtractionPersistence* S, FGuid P, int32 Quest, EExtractionEconomyAction Action)
	{
		return S->Economy(P, EconomyRequest(S, P, Action, NAME_None, 1, Quest));
	}

	bool TestRaid(UExtractionPersistence* S, FGuid P, EExtractionOutcome Outcome, int32 Kills = 0,
	              const FExtractionItem* Document = nullptr, bool FromRoom = false)
	{
		FGuid Raid;
		if (!S->BeginRaid({P}, Raid) || !S->ActivateRaid(Raid)) return false;
		auto Inventory = S->FindRaid(Raid)->Players[0].Original;
		FExtractionRaidProgress Progress;
		Progress.GuardKills = Kills;
		if (Document)
		{
			// 前面某局阵亡会丢掉背包；像玩家重新买一个那样补上，文件才有地方放。
			if (!Inventory.InSlot(ExtractionGear::BagSlot))
			{
				auto Bag = ExtractionItems::Make(TEXT("Bag_Hike"));
				Bag.Slot = ExtractionGear::BagSlot;
				Inventory.GearGrids = true;
				Inventory.Items.Add(Bag);
			}
			ExtractionItems::AutoPlace(Inventory, *Document);
			if (FromRoom) for (const auto& O : Document->Origins) Progress.FileLots.Add(O.Lot);
		}
		return S->Settle(Raid, P, Outcome, Inventory, Progress);
	}

	bool WriteProgressionFixture(const FString& Directory, FExtractionStoreState State)
	{
		auto& Files = FPlatformFileManager::Get().GetPlatformFile();
		Files.CreateDirectoryTree(*Directory);
		State.Sequence = 1;
		auto Object = FJsonObjectConverter::UStructToJsonObject(State);
		if (State.Version < 3)
			for (const auto& Value : Object->GetArrayField(TEXT("profiles")))
			{
				Value->AsObject()->RemoveField(TEXT("economyReceipts"));
				Value->AsObject()->GetObjectField(TEXT("view"))->RemoveField(TEXT("progression"));
			}
		FString Text;
		FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
		FTCHARToUTF8 Bytes(*Text);
		uint32 Header[] = {0x34455842, 1, uint32(Bytes.Length()), FCrc::MemCrc32(Bytes.Get(), Bytes.Length())};
		TUniquePtr<IFileHandle> File(Files.OpenWrite(*(Directory / TEXT("Store.journal"))));
		return File && File->Write(reinterpret_cast<const uint8*>(Header), sizeof(Header))
			&& File->Write(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length()) && File->Flush(true);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionProgressionEconomy, "BrokenEdge.Extraction.Progression.Economy",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionProgressionEconomy::RunTest(const FString&)
{
	const FString Directory = ProgressionDirectory();
	auto* S = ProgressionStore(Directory);
	FGuid P, Other;
	FString Credential;
	TestTrue(TEXT("Open profile"), S->OpenProfile({}, {}, P, Credential));
	TestTrue(TEXT("Open second profile"), S->OpenProfile({}, {}, Other, Credential));
	TestEqual(TEXT("Starter money"), S->View(P)->Progression.Credits, int64(2000));
	TestEqual(TEXT("Starter equipment unchanged"), S->View(P)->Stash.Count(TEXT("AK")), 3);
	auto Buy = EconomyRequest(S, P, EExtractionEconomyAction::Buy, TEXT("AK"));
	TestTrue(TEXT("Buy rifle"), S->Economy(P, Buy));
	const auto Purchased = S->View(P)->Stash.Items.Last();
	TestEqual(TEXT("No free magazine arbitrage"), Purchased.Magazine, 0);
	TestEqual(TEXT("Debit once"), S->View(P)->Progression.Credits, int64(800));
	TestTrue(TEXT("Exact duplicate succeeds"), S->Economy(P, Buy));
	TestEqual(TEXT("Duplicate keeps money"), S->View(P)->Progression.Credits, int64(800));
	TestFalse(TEXT("Level three ammo locked"), S->Economy(
		          P, EconomyRequest(S, P, EExtractionEconomyAction::Buy, TEXT("AmmoAP"))));
	TestFalse(
		TEXT("Insufficient money"), S->Economy(P, EconomyRequest(S, P, EExtractionEconomyAction::Buy, TEXT("AK"))));
	auto Sell = EconomyRequest(S, P, EExtractionEconomyAction::Sell);
	Sell.Item = Purchased.Id;
	TestTrue(TEXT("Sell owned rifle"), S->Economy(P, Sell));
	TestEqual(TEXT("Half-price recovery"), S->View(P)->Progression.Credits, int64(1400));
	TestTrue(TEXT("Old successful request after another transaction remains idempotent"), S->Economy(P, Buy));
	TestEqual(TEXT("Old request cannot buy again"), S->View(P)->Stash.Count(TEXT("AK")), 3);
	Buy.Revision = S->View(P)->Revision;
	TestFalse(TEXT("Same operation with fresh revision rejected"), S->Economy(P, Buy));
	Sell.Operation = FGuid::NewGuid();
	Sell.Item = S->View(Other)->Stash.Items[0].Id;
	Sell.Revision = S->View(P)->Revision;
	TestFalse(TEXT("Foreign item rejected"), S->Economy(P, Sell));
	for (int32 N : {-1, 0, 601, MAX_int32})
		TestFalse(
			TEXT("Invalid quantity rejected"),
			S->Economy(P, EconomyRequest(S, P, EExtractionEconomyAction::Buy, TEXT("Ammo"), N)));
	const auto* Rules = GetDefault<UExtractionProgressionRules>();
	auto Gun = ExtractionItems::Make(TEXT("AK"));
	Gun.MagazineAmmo = TEXT("AmmoAP");
	Gun.Magazine = 17;
	FExtractionAttachment Part;
	Part.Id = FGuid::NewGuid();
	Part.Definition = TEXT("RedDot");
	Gun.Attachments.Add(Part);
	TestEqual(TEXT("Whole weapon includes magazine and parts"), Rules->SellPrice(Gun, 1), int64(801));
	auto Armor = ExtractionItems::Make(TEXT("Helmet2"));
	Armor.Durability = 15;
	TestEqual(TEXT("Quarter durability valuation"), Rules->SellPrice(Armor, 1), int64(50));
	auto Vest = ExtractionItems::Make(TEXT("Vest2"));
	Vest.Durability = 56;
	TestEqual(TEXT("Integral durability price must not round down due to float ratio"), Rules->SellPrice(Vest, 1),
	          int64(210));
	Vest.Durability = 55;
	TestEqual(TEXT("Fractional valuation still floors"), Rules->SellPrice(Vest, 1), int64(206));
	Armor.Durability = 0;
	TestEqual(TEXT("Broken armor zero recovery"), Rules->SellPrice(Armor, 1), int64(0));
	TestEqual(TEXT("Documents cannot be sold"), Rules->SellPrice(ExtractionItems::Make(TEXT("Document")), 1),
	          int64(-1));
	TestEqual(TEXT("Valuable-only purchase absent"), Rules->Offer(TEXT("Electronics")),
	          static_cast<const FExtractionOffer*>(nullptr));
	S = ProgressionStore(Directory);
	TestTrue(TEXT("Persisted sell receipt"), S->Economy(
		         P, EconomyRequest(S, P, EExtractionEconomyAction::AcceptQuest, NAME_None, 1, 0)));
	Sell = EconomyRequest(S, P, EExtractionEconomyAction::Sell);
	Sell.Item = S->View(P)->Stash.Items[1].Id;
	Sell.Quantity = 30;
	TestTrue(TEXT("Partial stack sale"), S->Economy(P, Sell));
	TestEqual(TEXT("Partial sale count"), S->View(P)->Stash.Count(TEXT("Ammo")), 330);
	TestEqual(TEXT("Second profile isolated"), S->View(Other)->Progression.Credits, int64(2000));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionProgressionQuests, "BrokenEdge.Extraction.Progression.Quests",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionProgressionQuests::RunTest(const FString&)
{
	auto* S = ProgressionStore(ProgressionDirectory());
	FGuid P;
	FString Credential;
	S->OpenProfile({}, {}, P, Credential);
	TestFalse(TEXT("Chain cannot skip first task"), QuestOperation(S, P, 1, EExtractionEconomyAction::AcceptQuest));
	TestTrue(TEXT("Accept first task"), QuestOperation(S, P, 0, EExtractionEconomyAction::AcceptQuest));
	for (auto O : {
		     EExtractionOutcome::Dead, EExtractionOutcome::Left, EExtractionOutcome::Disconnected,
		     EExtractionOutcome::TimedOut
	     })
	{
		TestTrue(TEXT("Failed raid settles"), TestRaid(S, P, O, 9));
		TestEqual(TEXT("Failure never advances first task"), S->View(P)->Progression.Quests[0].Progress, 0);
	}
	TestTrue(TEXT("Successful extraction advances task"), TestRaid(S, P, EExtractionOutcome::Extracted));
	TestEqual(TEXT("No automatic reward"), S->View(P)->Progression.Credits, int64(2000));
	auto Claim = EconomyRequest(S, P, EExtractionEconomyAction::ClaimQuest, NAME_None, 1, 0);
	TestTrue(TEXT("Claim first reward"), S->Economy(P, Claim));
	TestTrue(TEXT("Duplicate reward request"), S->Economy(P, Claim));
	TestFalse(TEXT("New request cannot claim twice"), QuestOperation(S, P, 0, EExtractionEconomyAction::ClaimQuest));
	TestEqual(TEXT("Level two"), GetDefault<UExtractionProgressionRules>()->Level(S->View(P)->Progression.Experience),
	          2);
	TestTrue(TEXT("Accept guard task"), QuestOperation(S, P, 1, EExtractionEconomyAction::AcceptQuest));
	TestRaid(S, P, EExtractionOutcome::Extracted, 1);
	TestRaid(S, P, EExtractionOutcome::Dead, 2);
	TestEqual(TEXT("Failed kills excluded; saved kills retained"), S->View(P)->Progression.Quests[1].Progress, 1);
	FGuid Unfinished;
	S->BeginRaid({P}, Unfinished);
	S->ActivateRaid(Unfinished);
	TestFalse(TEXT("Trading cannot occur in active raid"), S->Economy(
		          P, EconomyRequest(S, P, EExtractionEconomyAction::Buy, TEXT("Ammo"))));
	S->Settle(Unfinished, P, EExtractionOutcome::Extracted, S->FindRaid(Unfinished)->Players[0].Original, {1, {}});
	TestTrue(TEXT("Claim guard task"), QuestOperation(S, P, 1, EExtractionEconomyAction::ClaimQuest));
	TestEqual(TEXT("Level three"), GetDefault<UExtractionProgressionRules>()->Level(S->View(P)->Progression.Experience),
	          3);
	TestTrue(TEXT("Accept final task"), QuestOperation(S, P, 2, EExtractionEconomyAction::AcceptQuest));
	auto OldFile = ExtractionItems::Make(TEXT("Document"));
	TestRaid(S, P, EExtractionOutcome::Extracted, 0, &OldFile, false);
	TestFalse(
		TEXT("Unqualified or previously brought-in file cannot be delivered"),
		QuestOperation(S, P, 2, EExtractionEconomyAction::DeliverQuest));
	auto File = ExtractionItems::Make(TEXT("Document"));
	TestRaid(S, P, EExtractionOutcome::Dead, 0, &File, true);
	TestFalse(TEXT("Death cannot qualify a file"), QuestOperation(S, P, 2, EExtractionEconomyAction::DeliverQuest));
	File = ExtractionItems::Make(TEXT("Document"));
	TestRaid(S, P, EExtractionOutcome::Extracted, 0, &File, true);
	TestTrue(TEXT("Deliver qualified file atomically"), QuestOperation(
		         S, P, 2, EExtractionEconomyAction::DeliverQuest));
	TestTrue(TEXT("Qualified file removed"), S->View(P)->Stash.Find(File.Id) == nullptr);
	TestTrue(TEXT("Unqualified file retained"), S->View(P)->Stash.Find(OldFile.Id) != nullptr);
	TestFalse(TEXT("Cannot deliver twice"), QuestOperation(S, P, 2, EExtractionEconomyAction::DeliverQuest));
	TestTrue(TEXT("Claim final task"), QuestOperation(S, P, 2, EExtractionEconomyAction::ClaimQuest));
	TestEqual(TEXT("All rewards once"), S->View(P)->Progression.Credits, int64(5000));
	TestEqual(TEXT("All experience preserved"), S->View(P)->Progression.Experience, int64(600));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionProgressionMigration, "BrokenEdge.Extraction.Progression.Migration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionProgressionMigration::RunTest(const FString&)
{
	for (int32 Version : {1, 2})
	{
		auto* Original = ProgressionStore(ProgressionDirectory());
		FGuid P;
		FString Credential;
		Original->OpenProfile({}, {}, P, Credential);
		auto State = Original->TestState();
		State.Version = Version;
		const auto Rifle = State.Profiles[0].View.Stash.Items[0];
		const FString Directory = ProgressionDirectory();
		TestTrue(TEXT("Write historical format without progression"), WriteProgressionFixture(Directory, State));
		TArray<uint8> Before, Backup;
		FFileHelper::LoadFileToArray(Before, *(Directory / TEXT("Store.journal")));
		auto* S = ProgressionStore(Directory);
		TestTrue(TEXT("Migrate old store"), S->IsAvailable());
		TestEqual(TEXT("Current payload version"), S->TestState().Version, 8);
		TestEqual(TEXT("Exactly one starting grant"), S->View(P)->Progression.Credits, int64(2000));
		TestEqual(TEXT("Rifle identity unchanged"), S->View(P)->Stash.Items[0].Id, Rifle.Id);
		TestEqual(TEXT("Rifle provenance unchanged"), S->View(P)->Stash.Items[0].Origins[0].Lot, Rifle.Origins[0].Lot);
		FFileHelper::LoadFileToArray(Backup, *(Directory / FString::Printf(TEXT("Store.journal.v%d.bak"), Version)));
		TestTrue(TEXT("Byte-exact source backup"), Before == Backup);
		for (int32 Restart = 0; Restart < 2; ++Restart)
		{
			S = ProgressionStore(Directory);
			TestEqual(TEXT("Restart grant stays one"), S->View(P)->Progression.Credits, int64(2000));
		}
		const FString Blocked = ProgressionDirectory();
		WriteProgressionFixture(Blocked, State);
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectory(
			*(Blocked / FString::Printf(TEXT("Store.journal.v%d.bak"), Version)));
		TestFalse(TEXT("Backup failure blocks migration"), ProgressionStore(Blocked)->IsAvailable());
		TArray<uint8> After;
		FFileHelper::LoadFileToArray(After, *(Blocked / TEXT("Store.journal")));
		TestTrue(TEXT("Failed backup leaves original bytes untouched"), Before == After);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionProgressionAtomicity, "BrokenEdge.Extraction.Progression.Atomicity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionProgressionAtomicity::RunTest(const FString&)
{
	const FString Directory = ProgressionDirectory();
	auto* S = ProgressionStore(Directory);
	FGuid P;
	FString Credential;
	S->OpenProfile({}, {}, P, Credential);
	auto Buy = EconomyRequest(S, P, EExtractionEconomyAction::Buy, TEXT("Key"));
	S->FailNextWrite = true;
	AddExpectedError(TEXT("EXTRACTION_STORE_WRITE_FAILED"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Failed journal write rejects transaction"), S->Economy(P, Buy));
	TestEqual(TEXT("No debit on write failure"), S->View(P)->Progression.Credits, int64(2000));
	TestEqual(TEXT("No new item on write failure"), S->View(P)->Stash.Count(TEXT("Key")), 0);
	S = ProgressionStore(Directory);
	TestTrue(TEXT("Same request can retry after failed write"), S->Economy(P, Buy));
	for (int32 Restart = 0; Restart < 2; ++Restart)
	{
		S = ProgressionStore(Directory);
		TestTrue(TEXT("Unacknowledged committed operation replays safely"), S->Economy(P, Buy));
		TestEqual(TEXT("Restart does not duplicate key"), S->View(P)->Stash.Count(TEXT("Key")), 1);
		TestEqual(TEXT("Restart does not debit twice"), S->View(P)->Progression.Credits, int64(1700));
	}
	auto Full = S->TestState();
	auto& Stash = Full.Profiles[0].View.Stash;
	Stash.Items.Reset();
	for (int32 I = 0; I < 119; ++I) ExtractionItems::AutoPlace(Stash, ExtractionItems::Make(TEXT("Ammo"), 60));
	const FString FullDirectory = ProgressionDirectory();
	WriteProgressionFixture(FullDirectory, Full);
	S = ProgressionStore(FullDirectory);
	const int32 Count = S->View(P)->Stash.Count(TEXT("Ammo"));
	TestFalse(TEXT("Multi-item buy fails atomically when only one cell fits"), S->Economy(
		          P, EconomyRequest(S, P, EExtractionEconomyAction::Buy, TEXT("Key"), 2)));
	TestEqual(TEXT("Failed batch leaves no partial key"), S->View(P)->Stash.Count(TEXT("Key")), 0);
	TestEqual(TEXT("Existing stacks untouched"), S->View(P)->Stash.Count(TEXT("Ammo")), Count);
	TestEqual(TEXT("Batch failure does not charge"), S->View(P)->Progression.Credits, int64(1700));
	// Every irreversible economy branch uses the same failed-write boundary.
	for (auto Action : {
		     EExtractionEconomyAction::Sell, EExtractionEconomyAction::DeliverQuest,
		     EExtractionEconomyAction::ClaimQuest
	     })
	{
		const FString FailureDirectory = ProgressionDirectory();
		auto* F = ProgressionStore(FailureDirectory);
		FGuid Owner;
		F->OpenProfile({}, {}, Owner, Credential);
		QuestOperation(F, Owner, 0, EExtractionEconomyAction::AcceptQuest);
		TestRaid(F, Owner, EExtractionOutcome::Extracted);
		QuestOperation(F, Owner, 0, EExtractionEconomyAction::ClaimQuest);
		QuestOperation(F, Owner, 1, EExtractionEconomyAction::AcceptQuest);
		TestRaid(F, Owner, EExtractionOutcome::Extracted, 2);
		QuestOperation(F, Owner, 1, EExtractionEconomyAction::ClaimQuest);
		QuestOperation(F, Owner, 2, EExtractionEconomyAction::AcceptQuest);
		auto File = ExtractionItems::Make(TEXT("Document"));
		TestRaid(F, Owner, EExtractionOutcome::Extracted, 0, &File, true);
		if (Action == EExtractionEconomyAction::ClaimQuest)
			QuestOperation(F, Owner, 2, EExtractionEconomyAction::DeliverQuest);
		auto Request = EconomyRequest(F, Owner, Action, NAME_None, 1,
		                              Action == EExtractionEconomyAction::Sell ? INDEX_NONE : 2);
		if (Action == EExtractionEconomyAction::Sell) Request.Item = F->View(Owner)->Stash.Items[0].Id;
		FString Before, After;
		FJsonObjectConverter::UStructToJsonObjectString(*F->View(Owner), Before);
		F->FailNextWrite = true;
		AddExpectedError(TEXT("EXTRACTION_STORE_WRITE_FAILED"), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("Sell, delivery and reward reject failed persistence"), F->Economy(Owner, Request));
		FJsonObjectConverter::UStructToJsonObjectString(*F->View(Owner), After);
		TestEqual(TEXT("Write failure preserves entire profile"), After, Before);
		F = ProgressionStore(FailureDirectory);
		FJsonObjectConverter::UStructToJsonObjectString(*F->View(Owner), After);
		TestEqual(TEXT("Restart after failed write preserves entire profile"), After, Before);
		TestTrue(TEXT("Failed operation can retry once"), F->Economy(Owner, Request));
		TestTrue(TEXT("Retry receipt is idempotent"), F->Economy(Owner, Request));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionEconomyCrashBoundary, "BrokenEdge.Extraction.EconomyCrashBoundary",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionEconomyCrashBoundary::RunTest(const FString&)
{
	FString Path, Boundary;
	if (!FParse::Value(FCommandLine::Get(), TEXT("ExtractionBoundaryDir="), Path)
		|| !FParse::Value(FCommandLine::Get(), TEXT("ExtractionBoundary="), Boundary))
		return true;
	const bool Before = Boundary.EndsWith(TEXT("-before"));
	const bool Settlement = Boundary.StartsWith(TEXT("settlement"));
	const bool Buy = Boundary.StartsWith(TEXT("buy")), Sell = Boundary.StartsWith(TEXT("sell"));
	const bool Claim = Boundary.StartsWith(TEXT("claim"));
	auto* S = ProgressionStore(Path);
	if (!TestTrue(TEXT("Boundary store available"), S->IsAvailable())) return false;
	FExtractionEconomyRequest Request;
	FExtractionContainer Inventory;
	FExtractionRaidProgress Progress;
	FGuid P, Raid;
	const FString RequestPath = Path / TEXT("Request.json");
	if (FParse::Param(FCommandLine::Get(), TEXT("ExtractionVerifyRecovery")))
	{
		if (!TestEqual(TEXT("Single durable identity"), S->TestState().Profiles.Num(), 1)) return false;
		P = S->TestState().Profiles[0].View.Profile;
		int32 Attempt = 1;
		FParse::Value(FCommandLine::Get(), TEXT("ExtractionRecoveryAttempt="), Attempt);
		const bool NotCommitted = Before && Attempt == 1;
		auto Verify = [&](bool Pending)
		{
			const auto& V = *S->View(P);
			const int64 Money = Buy
				                    ? (Pending ? 2000 : 1700)
				                    : Sell
				                    ? (Pending ? 2000 : 2630)
				                    : Claim && !Pending
				                    ? 5000
				                    : 3500;
			TestEqual(TEXT("Money conserved across killed process"), V.Progression.Credits, Money);
			TestEqual(TEXT("XP conserved across killed process"), V.Progression.Experience,
			          int64(Buy || Sell ? 0 : Claim && !Pending ? 600 : 300));
			TestEqual(TEXT("Rifle quantity conserved"), V.Stash.Count(TEXT("AK")), Sell && !Pending ? 2 : 3);
			TestEqual(TEXT("Key quantity conserved"), V.Stash.Count(TEXT("Key")), Buy && !Pending ? 1 : 0);
			if (!Buy && !Sell)
			{
				const auto& Q = V.Progression.Quests[2];
				TestEqual(TEXT("Document consumed and progress committed together"), V.Stash.Count(TEXT("Document")),
				          Settlement ? (Before ? 0 : 1) : Claim ? 0 : Pending ? 1 : 0);
				TestEqual(TEXT("Delivery state conserved"), Q.Delivered, !Settlement && (Claim || !Pending));
				TestEqual(TEXT("Reward state conserved"), Q.Claimed, Claim && !Pending);
				if (Settlement)
				{
					TestEqual(TEXT("Recovered raid cannot qualify new file"), Q.EligibleFiles.Num(), Before ? 0 : 1);
					TestTrue(TEXT("Recovered or successful outcome remains terminal"), V.Result.Outcome ==
					         (Before ? EExtractionOutcome::Recovered : EExtractionOutcome::Extracted));
				}
			}
		};
		Verify(NotCommitted);
		if (!Settlement)
		{
			FString Json;
			if (!FFileHelper::LoadFileToString(Json, *RequestPath)
				|| !FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Request))
				return false;
			TestTrue(TEXT("Unacknowledged request retried after process restart"), S->Economy(P, Request));
			TestTrue(TEXT("Duplicate request in fresh process remains harmless"), S->Economy(P, Request));
			Verify(false);
		}
		FString Snapshot, Previous;
		FJsonObjectConverter::UStructToJsonObjectString(*S->View(P), Snapshot);
		const FString SnapshotPath = Path / TEXT("VerifiedProfile.json");
		if (Attempt == 1) FFileHelper::SaveStringToFile(Snapshot, *SnapshotPath);
		else
		{
			FFileHelper::LoadFileToString(Previous, *SnapshotPath);
			TestEqual(TEXT("Second independent restart preserves all identities and revisions"), Snapshot, Previous);
		}
		return true;
	}
	FString Credential;
	if (!S->OpenProfile({}, {}, P, Credential)) return false;
	if (!Buy && !Sell)
	{
		if (!QuestOperation(S, P, 0, EExtractionEconomyAction::AcceptQuest)
			|| !TestRaid(S, P, EExtractionOutcome::Extracted)
			|| !QuestOperation(S, P, 0, EExtractionEconomyAction::ClaimQuest)
			|| !QuestOperation(S, P, 1, EExtractionEconomyAction::AcceptQuest)
			|| !TestRaid(S, P, EExtractionOutcome::Extracted, 2)
			|| !QuestOperation(S, P, 1, EExtractionEconomyAction::ClaimQuest)
			|| !QuestOperation(S, P, 2, EExtractionEconomyAction::AcceptQuest))
			return false;
		auto File = ExtractionItems::Make(TEXT("Document"));
		if (Settlement)
		{
			if (!S->BeginRaid({P}, Raid) || !S->ActivateRaid(Raid)) return false;
			Inventory = S->FindRaid(Raid)->Players[0].Original;
			ExtractionItems::AutoPlace(Inventory, File);
			for (const auto& Origin : File.Origins) Progress.FileLots.Add(Origin.Lot);
		}
		else if (!TestRaid(S, P, EExtractionOutcome::Extracted, 0, &File, true)) return false;
		if (Claim && !QuestOperation(S, P, 2, EExtractionEconomyAction::DeliverQuest)) return false;
	}
	if (!Settlement)
	{
		Request = EconomyRequest(S, P, Buy
			                               ? EExtractionEconomyAction::Buy
			                               : Sell
			                               ? EExtractionEconomyAction::Sell
			                               : Claim
			                               ? EExtractionEconomyAction::ClaimQuest
			                               : EExtractionEconomyAction::DeliverQuest,
		                         Buy ? FName(TEXT("Key")) : NAME_None, 1, Buy || Sell ? INDEX_NONE : 2);
		if (Sell) Request.Item = S->View(P)->Stash.Items[0].Id;
		FString Json;
		FJsonObjectConverter::UStructToJsonObjectString(Request, Json);
		if (!FFileHelper::SaveStringToFile(Json, *RequestPath)) return false;
	}
	if (!Before && !(Settlement
		                 ? S->Settle(Raid, P, EExtractionOutcome::Extracted, Inventory, Progress)
		                 : S->Economy(P, Request)))
		return false;
	if (!FFileHelper::SaveStringToFile(TEXT("durable-boundary"), *(Path / TEXT("Ready.txt")))) return false;
	for (;;) FPlatformProcess::Sleep(1);
}

// 钥匙卡商店：六张航天基地卡都必须可购买、买入即补满开门次数、半价回收，
// 并能从仓库移入配装，随出战票据带入战局。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionKeyCardShop, "BrokenEdge.Extraction.Progression.KeyCardShop",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionKeyCardShop::RunTest(const FString&)
{
	const auto* Rules = GetDefault<UExtractionProgressionRules>();
	const TArray<FName> Cards = {
		TEXT("Key_Contract"), TEXT("Key_StaffPassage"), TEXT("Key_EastBridge"),
		TEXT("Key_Monitor"), TEXT("Key_President"), TEXT("Key_Core")
	};
	for (FName Card : Cards)
	{
		const FString Name = Card.ToString();
		const auto* O = Rules->Offer(Card);
		TestNotNull(*FString::Printf(TEXT("%s 可购买"), *Name), O);
		if (!O) continue;
		const auto* D = UExtractionItemDefinition::Get(Card);
		TestNotNull(*FString::Printf(TEXT("%s 定义可加载"), *Name), D);
		if (!D) continue;
		TestTrue(*FString::Printf(TEXT("%s 是钥匙卡"), *Name), D->Kind == EExtractionItemKind::Key);
		TestTrue(*FString::Printf(TEXT("%s 有开门次数"), *Name), D->KeyUses >= 1);
		TestTrue(*FString::Printf(TEXT("%s 价格与等级合法"), *Name),
		         O->Price > 0 && O->Price <= 1000000 && O->Level >= 1 && O->Level <= 3);
		const auto Instance = ExtractionItems::Make(Card);
		TestEqual(*FString::Printf(TEXT("%s 买入补满次数"), *Name), Instance.Durability, float(D->KeyUses));
		TestEqual(*FString::Printf(TEXT("%s 半价回收"), *Name), Rules->SellPrice(Instance, 1), int64(O->Price / 2));
	}
	TestTrue(TEXT("商品表配置合法"), Rules->IsValidConfiguration());

	const FString Directory = ProgressionDirectory();
	auto* S = ProgressionStore(Directory);
	FGuid P;
	FString Credential;
	TestTrue(TEXT("开档"), S->OpenProfile({}, {}, P, Credential));
	TestTrue(TEXT("买入合同房钥匙卡"),
	         S->Economy(P, EconomyRequest(S, P, EExtractionEconomyAction::Buy, TEXT("Key_Contract"))));
	TestEqual(TEXT("按标价扣款"), S->View(P)->Progression.Credits, int64(2000 - 700));
	const auto* Bought = S->View(P)->Stash.Items.FindByPredicate([](const auto& I)
	{
		return I.Definition == TEXT("Key_Contract");
	});
	TestNotNull(TEXT("钥匙卡进入仓库"), Bought);
	if (!Bought) return false;
	FExtractionStorageRequest Equip;
	Equip.Operation = FGuid::NewGuid();
	Equip.Revision = S->View(P)->Revision;
	Equip.From = EExtractionStorageSide::Stash;
	Equip.To = EExtractionStorageSide::Loadout;
	Equip.Move.Item = Bought->Id;
	Equip.Move.Slot = -1;
	TestTrue(TEXT("移入配装"), S->Move(P, Equip));
	TestEqual(TEXT("仓库交出钥匙卡"), S->View(P)->Stash.Count(TEXT("Key_Contract")), 0);
	TestEqual(TEXT("配装持有钥匙卡"), S->View(P)->Loadout.Count(TEXT("Key_Contract")), 1);
	FGuid Raid;
	TestTrue(TEXT("准备出战"), S->BeginRaid({P}, Raid));
	const auto* Record = S->FindRaid(Raid);
	TestNotNull(TEXT("出战票据存在"), Record);
	if (!Record) return false;
	TestEqual(TEXT("票据携带钥匙卡"), Record->Players[0].Original.Count(TEXT("Key_Contract")), 1);
	TestTrue(TEXT("出战清空配装"), S->View(P)->Loadout.Items.IsEmpty());
	TestTrue(TEXT("取消未激活出战"), S->AbortRaid(Raid));
	TestEqual(TEXT("取消后钥匙卡退回仓库"), S->View(P)->Stash.Count(TEXT("Key_Contract")), 1);
	return true;
}
#endif
