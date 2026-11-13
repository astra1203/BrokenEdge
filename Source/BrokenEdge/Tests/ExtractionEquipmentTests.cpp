#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BrokenEdge/Extraction/ExtractionItems.h"
#include "BrokenEdge/Extraction/ExtractionHealth.h"
#include "BrokenEdge/Extraction/ExtractionPersistence.h"
#include "Engine/GameInstance.h"
#include "JsonObjectConverter.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString EquipmentSnapshot(const FExtractionContainer& C)
	{
		FString Text;
		FJsonObjectConverter::UStructToJsonObjectString(C, Text);
		return Text;
	}

	FString EquipmentDirectory()
	{
		return FPaths::ProjectSavedDir() / TEXT("Extraction/EquipmentTests") / FGuid::NewGuid().ToString();
	}

	UExtractionPersistence* EquipmentStore(const FString& Directory)
	{
		auto* S = NewObject<UExtractionPersistence>(NewObject<UGameInstance>());
		S->LoadForTest(Directory);
		return S;
	}

	bool EquipmentJournal(const FString& Directory, const FExtractionStoreState& State, bool Legacy = false)
	{
		FString Json;
		FJsonObjectConverter::UStructToJsonObjectString(State, Json, 0, 0, 0, nullptr, false);
		if (Legacy)
		{
			// Simulate the exact old payload: fields absent, not merely present with defaults.
			TSharedPtr<FJsonObject> Root;
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root)) return false;
			TFunction<void(const TSharedPtr<FJsonValue>&)> Strip = [&](const TSharedPtr<FJsonValue>& Value)
			{
				if (Value->Type == EJson::Object)
				{
					auto Object = Value->AsObject();
					for (const TCHAR* Field : {TEXT("magazineAmmo"), TEXT("durability"), TEXT("attachments")})
						Object->
							RemoveField(Field);
					for (const auto& Field : Object->Values) Strip(Field.Value);
				}
				else if (Value->Type == EJson::Array) for (const auto& Child : Value->AsArray()) Strip(Child);
			};
			Strip(MakeShared<FJsonValueObject>(Root));
			Json.Reset();
			if (!FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Json))) return false;
		}
		FTCHARToUTF8 Bytes(*Json);
		uint32 Header[] = {
			0x34455842, 1, static_cast<uint32>(Bytes.Length()), FCrc::MemCrc32(Bytes.Get(), Bytes.Length())
		};
		auto& Files = FPlatformFileManager::Get().GetPlatformFile();
		Files.CreateDirectoryTree(*Directory);
		TUniquePtr<IFileHandle> File(Files.OpenWrite(*(Directory / TEXT("Store.journal"))));
		return File && File->Write(reinterpret_cast<uint8*>(Header), sizeof(Header))
			&& File->Write(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length()) && File->Flush(true);
	}

	FExtractionStoredProfile EquipmentProfile(FGuid Store)
	{
		FExtractionStoredProfile P;
		P.View.Progression.Initialize();
		P.View.Store = Store;
		P.View.Profile = FGuid::NewGuid();
		P.View.Revision = 1;
		P.CredentialHash = FString::ChrN(40, '0');
		P.View.Stash.Id = FGuid::NewGuid();
		P.View.Stash.Width = 10;
		P.View.Stash.Height = 12;
		P.View.Loadout.Id = FGuid::NewGuid();
		P.View.Loadout.WeaponSlots = true;
		return P;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionEquipmentTransactions,
                                 "BrokenEdge.Extraction.Equipment.AtomicConfiguration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionEquipmentTransactions::RunTest(const FString&)
{
	FString Error;
	FExtractionContainer C;
	C.WeaponSlots = true;
	for (auto Id : UExtractionItemDefinition::Ids())
		if (!TestNotNull(*Id.ToString(),
		                 UExtractionItemDefinition::Get(Id)))
			return false;
	auto Gun = ExtractionItems::Make(TEXT("AK"));
	Gun.Slot = 0;
	C.Items.Add(Gun);
	auto Grip = ExtractionItems::Make(TEXT("VerticalGrip"));
	ExtractionItems::AutoPlace(C, Grip);
	FExtractionItemRequest R;
	R.Item = Grip.Id;
	R.Weapon = Gun.Id;
	R.Op = EExtractionItemOp::Attach;
	TestTrue(TEXT("Install original grip"), ExtractionItems::Configure(C, R, Error));
	TestNull(TEXT("Mounted instance leaves grid"), C.Find(Grip.Id));
	TestEqual(TEXT("Mounted identity retained"), C.Find(Gun.Id)->Attachments[0].Id, Grip.Id);
	TestEqual(TEXT("Scatter effect present"), ExtractionItems::ScatterScale(*C.Find(Gun.Id)), .8f);
	const FString Attached = EquipmentSnapshot(C);
	TestFalse(TEXT("Repeated attach cannot duplicate"), ExtractionItems::Configure(C, R, Error));
	TestEqual(TEXT("Rejected request preserves origins too"), EquipmentSnapshot(C), Attached);
	R.Item = Gun.Id;
	R.Weapon.Invalidate();
	R.Op = EExtractionItemOp::SelectAmmo;
	R.AmmoDefinition = TEXT("AmmoAP");
	TestFalse(TEXT("Cannot relabel loaded ordinary ammunition"), ExtractionItems::Configure(C, R, Error));
	R.Op = EExtractionItemOp::Unload;
	TestTrue(TEXT("Unload actual rounds"), ExtractionItems::Configure(C, R, Error));
	TestEqual(TEXT("30 rounds returned"), C.Count(TEXT("Ammo")), 30);
	TestEqual(TEXT("Empty magazine"), C.Find(Gun.Id)->Magazine, 0);
	R.Op = EExtractionItemOp::SelectAmmo;
	TestTrue(TEXT("Empty magazine accepts new type"), ExtractionItems::Configure(C, R, Error));
	TestEqual(TEXT("AP selection retained"), C.Find(Gun.Id)->MagazineAmmo, FName(TEXT("AmmoAP")));
	R.Op = EExtractionItemOp::Detach;
	R.AttachmentSlot = EExtractionAttachmentSlot::Grip;
	TestTrue(TEXT("Detach original instance"), ExtractionItems::Configure(C, R, Error));
	TestNotNull(TEXT("Detached identity back in bag"), C.Find(Grip.Id));
	TestEqual(TEXT("Attachment removal restores scatter"), ExtractionItems::ScatterScale(*C.Find(Gun.Id)), 1.f);
	auto Vest = ExtractionItems::Make(TEXT("Vest2"));
	ExtractionItems::AutoPlace(C, Vest);
	R = {};
	R.Item = Vest.Id;
	R.Slot = 2;
	TestFalse(TEXT("Vest cannot occupy helmet slot"), ExtractionItems::Transfer(C, C, R, Error));
	R.Slot = 3;
	TestTrue(TEXT("Vest equips without using grid"), ExtractionItems::Transfer(C, C, R, Error));
	TestEqual(TEXT("Vest initial durability"), C.InSlot(3)->Durability, 80.f);
	FExtractionContainer Full;
	Full.Width = Full.Height = 1;
	Full.WeaponSlots = true;
	Full.Items.Add(Gun);
	ExtractionItems::AutoPlace(Full, ExtractionItems::Make(TEXT("Electronics")));
	R = {};
	R.Item = Gun.Id;
	R.Op = EExtractionItemOp::Unload;
	const FString Before = EquipmentSnapshot(Full);
	TestFalse(TEXT("Full bag rejects unload"), ExtractionItems::Configure(Full, R, Error));
	TestEqual(TEXT("Full bag preserves all 30 magazine origins"), EquipmentSnapshot(Full), Before);
	FExtractionAttachment Mounted;
	Mounted.Id = Grip.Id;
	Mounted.Definition = Grip.Definition;
	Mounted.Origins = Grip.Origins;
	Full.Find(Gun.Id)->Attachments.Add(Mounted);
	R.Op = EExtractionItemOp::Detach;
	R.AttachmentSlot = EExtractionAttachmentSlot::Grip;
	const FString BeforeDetach = EquipmentSnapshot(Full);
	TestFalse(TEXT("Full bag rejects detachment"), ExtractionItems::Configure(Full, R, Error));
	TestEqual(TEXT("Rejected detachment preserves mounted GUID and provenance"), EquipmentSnapshot(Full), BeforeDetach);
	auto Muzzle = ExtractionItems::Make(TEXT("Compensator"));
	Mounted.Id = Muzzle.Id;
	Mounted.Definition = Muzzle.Definition;
	Mounted.Origins = Muzzle.Origins;
	Full.Find(Gun.Id)->Attachments.Add(Mounted);
	TestTrue(TEXT("Grip and compensator multiply"), FMath::IsNearlyEqual(
		         ExtractionItems::ScatterScale(*Full.Find(Gun.Id)), .68f));
	Full.Find(Gun.Id)->Attachments.Last().Id = Grip.Id;
	TestFalse(TEXT("Nested GUID duplicates are invalid"), ExtractionItems::Validate(Full, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionEquipmentDamageRules, "BrokenEdge.Extraction.Equipment.ArmorCoverage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionEquipmentDamageRules::RunTest(const FString&)
{
	for (FName Bone : {FName("Head"), FName("head")})
		TestEqual(
			TEXT("Helmet covers both rig spellings"), UExtractionHealth::ArmorSlotForBone(Bone), 2);
	for (FName Bone : {FName("Hips"), FName("Spine"), FName("Spine1"), FName("Spine2"), FName("spine_03")})
		TestEqual(TEXT("Vest covers actual Stinger and mannequin torso"), UExtractionHealth::ArmorSlotForBone(Bone), 3);
	for (FName Bone : {FName("LeftUpLeg"), FName("RightLeg"), FName("LeftFoot"), FName("thigh_r")})
	{
		TestTrue(TEXT("Actual leg causes fracture"), UExtractionHealth::IsLeg(Bone));
		TestEqual(TEXT("Leg bypasses vest"), UExtractionHealth::ArmorSlotForBone(Bone), -1);
	}
	TestEqual(TEXT("Arm bypasses vest"), UExtractionHealth::ArmorSlotForBone(TEXT("LeftArm")), -1);
	float Armor = 80;
	TestEqual(TEXT("Ordinary round blocked 60 percent"), UExtractionHealth::Absorb(20, 1, 2, Armor), 8.f);
	TestEqual(TEXT("Durability equals absorbed damage"), Armor, 68.f);
	Armor = 80;
	TestTrue(TEXT("AP round retains 80 percent"), FMath::IsNearlyEqual(UExtractionHealth::Absorb(17, 3, 2, Armor),
	                                                                   13.6f));
	Armor = 3;
	TestEqual(TEXT("Breaking hit spills remaining damage"), UExtractionHealth::Absorb(20, 1, 2, Armor), 17.f);
	TestEqual(TEXT("Break clamps to zero"), Armor, 0.f);
	TestEqual(TEXT("Broken armor no protection"), UExtractionHealth::Absorb(20, 1, 2, Armor), 20.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionEquipmentMigration, "BrokenEdge.Extraction.Equipment.LegacyMigration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionEquipmentMigration::RunTest(const FString&)
{
	const FString Directory = EquipmentDirectory();
	FExtractionStoreState State;
	State.Version = 1;
	State.Sequence = 1;
	State.Id = FGuid::NewGuid();
	auto P = EquipmentProfile(State.Id);
	auto Gun = ExtractionItems::Make(TEXT("AK"));
	Gun.Magazine = 17;
	ExtractionItems::TakeOrigins(Gun.MagazineOrigins, 13);
	ExtractionItems::AutoPlace(P.View.Stash, Gun);
	State.Profiles.Add(P);
	if (!TestTrue(TEXT("Write legacy fixture"), EquipmentJournal(Directory, State, true))) return false;
	TArray<uint8> Before;
	FFileHelper::LoadFileToArray(Before, *(Directory / TEXT("Store.journal")));
	auto* S = EquipmentStore(Directory);
	if (!TestTrue(TEXT("Legacy store loads"), S->IsAvailable())) return false;
	TestEqual(TEXT("Migrated version"), S->TestState().Version, 8);
	// 版本 4：旧出战配置迁移为携行容器，补 24 格背包（与旧 6×4 一致）与基础安全箱。
	const auto& Loadout = S->View(P.View.Profile)->Loadout;
	TestTrue(TEXT("Migrated loadout uses gear grids"), Loadout.GearGrids);
	TestTrue(TEXT("Migrated loadout gains a 24-cell backpack"), Loadout.InSlot(ExtractionGear::BagSlot)
	         && Loadout.InSlot(ExtractionGear::BagSlot)->Definition == TEXT("Bag_Hike"));
	TestTrue(TEXT("Migrated loadout gains a basic safe box"), Loadout.InSlot(ExtractionGear::SafeSlot) != nullptr);
	const auto* Restored = S->View(P.View.Profile)->Stash.Find(Gun.Id);
	if (!TestNotNull(TEXT("Legacy identity retained"), Restored)) return false;
	TestEqual(TEXT("Legacy loaded rounds retained"), Restored->Magazine, 17);
	TestEqual(TEXT("Legacy rounds default to ordinary"), Restored->MagazineAmmo, FName(TEXT("Ammo")));
	TestTrue(TEXT("Legacy gun has no fabricated attachments"), Restored->Attachments.IsEmpty());
	TArray<uint8> Backup;
	FFileHelper::LoadFileToArray(Backup, *(Directory / TEXT("Store.journal.v1.bak")));
	TestTrue(TEXT("Exact pre-migration journal preserved"), Backup == Before);
	const int64 Sequence = S->TestState().Sequence;
	S = EquipmentStore(Directory);
	TestEqual(TEXT("Second restart does not migrate or grant again"), S->TestState().Sequence, Sequence);
	const FString BlockedDirectory = EquipmentDirectory();
	EquipmentJournal(BlockedDirectory, State, true);
	auto& Files = FPlatformFileManager::Get().GetPlatformFile();
	Files.CreateDirectory(*(BlockedDirectory / TEXT("Store.journal.v1.bak")));
	auto* Blocked = EquipmentStore(BlockedDirectory);
	TestFalse(TEXT("Backup failure blocks migration"), Blocked->IsAvailable());
	TArray<uint8> Untouched;
	FFileHelper::LoadFileToArray(Untouched, *(BlockedDirectory / TEXT("Store.journal")));
	TestTrue(TEXT("Backup failure leaves exact legacy bytes intact"), Untouched == Before);
	const FString InterruptedDirectory = EquipmentDirectory();
	EquipmentJournal(InterruptedDirectory, State, true);
	auto* Interrupted = NewObject<UExtractionPersistence>(NewObject<UGameInstance>());
	Interrupted->FailNextWrite = true;
	AddExpectedError(TEXT("EXTRACTION_STORE_WRITE_FAILED"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Migration append failure blocks use"), Interrupted->LoadForTest(InterruptedDirectory));
	TestEqual(TEXT("Failed append keeps in-memory legacy version"), Interrupted->TestState().Version, 1);
	Interrupted = EquipmentStore(InterruptedDirectory);
	TestTrue(TEXT("Migration retries safely after storage recovers"), Interrupted->IsAvailable());
	TestEqual(
		TEXT("Retry grants no extra legacy rounds"), Interrupted->View(P.View.Profile)->Stash.Find(Gun.Id)->Magazine,
		17);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtractionEquipmentRecovery, "BrokenEdge.Extraction.Equipment.NestedExportRecovery",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FExtractionEquipmentRecovery::RunTest(const FString&)
{
	for (bool ExportGun : {false, true})
	{
		const FString Directory = EquipmentDirectory();
		FExtractionStoreState State;
		State.Id = FGuid::NewGuid();
		State.Sequence = 1;
		auto A = EquipmentProfile(State.Id), B = EquipmentProfile(State.Id);
		auto Gun = ExtractionItems::Make(TEXT("AK"));
		Gun.Slot = 0;
		Gun.MagazineAmmo = TEXT("AmmoAP");
		auto Part = ExtractionItems::Make(TEXT("RedDot"));
		FExtractionAttachment Mounted;
		Mounted.Id = Part.Id;
		Mounted.Definition = Part.Definition;
		Mounted.Origins = Part.Origins;
		Gun.Attachments.Add(Mounted);
		A.View.Loadout.Items.Add(Gun);
		State.Profiles = {A, B};
		if (!TestTrue(TEXT("Write equipment fixture"), EquipmentJournal(Directory, State))) return false;
		auto* S = EquipmentStore(Directory);
		FExtractionStorageRequest Config;
		Config.Operation = FGuid::NewGuid();
		Config.Revision = S->View(A.View.Profile)->Revision;
		Config.From = Config.To = EExtractionStorageSide::Loadout;
		Config.Move.Item = Gun.Id;
		Config.Move.Op = EExtractionItemOp::Detach;
		Config.Move.AttachmentSlot = EExtractionAttachmentSlot::Optic;
		if (!TestTrue(TEXT("Lobby storage detaches through durable move endpoint"), S->Move(A.View.Profile, Config)))
			return false;
		Config.Operation = FGuid::NewGuid();
		Config.Revision = S->View(A.View.Profile)->Revision;
		Config.Move.Item = Part.Id;
		Config.Move.Weapon = Gun.Id;
		Config.Move.Op = EExtractionItemOp::Attach;
		if (!TestTrue(TEXT("Lobby storage remounts original instance"), S->Move(A.View.Profile, Config))) return false;
		FGuid Raid;
		if (!TestTrue(TEXT("Begin actual raid transaction"), S->BeginRaid({A.View.Profile, B.View.Profile}, Raid)))
			return false;
		S->ActivateRaid(Raid);
		auto Original = S->FindRaid(Raid)->Players[0].Original;
		FExtractionContainer Export;
		FString Error;
		FExtractionItemRequest R;
		R.Item = Gun.Id;
		R.Op = EExtractionItemOp::Detach;
		R.AttachmentSlot = EExtractionAttachmentSlot::Optic;
		TestTrue(TEXT("Detach during raid retains seeded provenance"), ExtractionItems::Configure(Original, R, Error));
		R = {};
		R.Item = ExportGun ? Gun.Id : Part.Id;
		TestTrue(
			TEXT("Other player takes gun or detached optic"), ExtractionItems::Transfer(Original, Export, R, Error));
		if (!TestTrue(
			TEXT("Other player commits extraction"),
			S->Settle(Raid, B.View.Profile, EExtractionOutcome::Extracted, Export)))
			return false;
		S = EquipmentStore(Directory); // Owner remains unsettled: crash recovery.
		for (int Restart = 0; Restart < 2; ++Restart)
		{
			if (!TestTrue(TEXT("Recovered store valid"), S->IsAvailable())) return false;
			const auto& StashA = S->View(A.View.Profile)->Stash;
			const auto& StashB = S->View(B.View.Profile)->Stash;
			if (ExportGun)
			{
				TestNull(TEXT("Exported gun never refunded"), StashA.Find(Gun.Id));
				TestNotNull(TEXT("Unexported optic returned separately"), StashA.Find(Part.Id));
				if (const auto* Exported = StashB.Find(Gun.Id))
				{
					TestEqual(TEXT("AP magazine stays AP"), Exported->MagazineAmmo, FName(TEXT("AmmoAP")));
					TestTrue(TEXT("Exported gun remains stripped"), Exported->Attachments.IsEmpty());
				}
				else AddError(TEXT("Exported gun missing"));
			}
			else
			{
				TestNotNull(TEXT("Detached optic retained by recipient"), StashB.Find(Part.Id));
				const auto* Refunded = StashA.Find(Gun.Id);
				if (TestNotNull(TEXT("Owner receives gun body"), Refunded))
					TestTrue(
						TEXT("Exported optic not recreated on gun"), Refunded->Attachments.IsEmpty());
				TestNull(TEXT("No duplicate loose optic"), StashA.Find(Part.Id));
			}
			S = EquipmentStore(Directory);
		}
	}
	return true;
}
#endif
