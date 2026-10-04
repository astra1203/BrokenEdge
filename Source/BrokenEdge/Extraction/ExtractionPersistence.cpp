#include "ExtractionPersistence.h"
#include "JsonObjectConverter.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Misc/Crc.h"

namespace
{
	constexpr uint32 Magic = 0x34455842;
	constexpr uint32 MaxRecord = 64 * 1024 * 1024;

	FString Hash(const FString& Text)
	{
		FTCHARToUTF8 Utf8(*Text);
		FSHAHash Digest;
		FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Digest.Hash);
		return Digest.ToString();
	}

	FExtractionStoredProfile* ProfileIn(FExtractionStoreState& State, FGuid Id)
	{
		return State.Profiles.FindByPredicate([&](const auto& P) { return P.View.Profile == Id; });
	}

	FExtractionRaidRecord* RaidIn(FExtractionStoreState& State, FGuid Id)
	{
		return State.Raids.FindByPredicate([&](const auto& R) { return R.Id == Id; });
	}

	int32 Count(const TArray<FExtractionOrigin>& Origins, FGuid Lot)
	{
		int32 Total = 0;
		for (const auto& P : Origins) if (P.Lot == Lot) Total += P.Quantity;
		return Total;
	}

	bool ValidItem(const FExtractionItem& I)
	{
		FString Why;
		return ExtractionItems::ValidateItem(I, true, Why);
	}

	bool PreserveLegacyJournal(IPlatformFile& Files, const FString& Journal, int64 ValidEnd)
	{
		const FString Backup = Journal + TEXT(".v1.bak");
		if (!Files.FileExists(*Backup))
		{
			const FString Temporary = Backup + TEXT(".tmp");
			Files.DeleteFile(*Temporary);
			if (!Files.CopyFile(*Temporary, *Journal) || !Files.MoveFile(*Backup, *Temporary))
			{
				Files.DeleteFile(*Temporary);
				return false;
			}
		}
		// A retry after a torn migration may have a different incomplete tail. All
		// validated legacy records must still agree with the original byte backup.
		TUniquePtr<IFileHandle> Current(Files.OpenRead(*Journal)), Original(Files.OpenRead(*Backup));
		if (!Current || !Original || Original->Size() < ValidEnd) return false;
		uint8 A[4096], B[4096];
		for (int64 Offset = 0; Offset < ValidEnd;)
		{
			const int64 Count = FMath::Min<int64>(sizeof(A), ValidEnd - Offset);
			if (!Current->Read(A, Count) || !Original->Read(B, Count) || FMemory::Memcmp(A, B, Count)) return false;
			Offset += Count;
		}
		return true;
	}
}

FString UExtractionPersistence::OutcomeLabel(EExtractionOutcome O)
{
	switch (O)
	{
	case EExtractionOutcome::Extracted: return TEXT("撤离成功");
	case EExtractionOutcome::Dead: return TEXT("阵亡 · 物资损失");
	case EExtractionOutcome::Left: return TEXT("主动退出 · 物资损失");
	case EExtractionOutcome::Disconnected: return TEXT("连接中断 · 物资损失");
	case EExtractionOutcome::TimedOut: return TEXT("未及时撤离 · 物资损失");
	case EExtractionOutcome::Recovered: return TEXT("服务器恢复 · 未带出物资已返还");
	case EExtractionOutcome::Aborted: return TEXT("开局取消 · 带入物资已返还");
	default: return FString();
	}
}

bool UExtractionPersistence::EnsureLoaded()
{
	if (Loaded) return !Faulted;
	FString Directory = FPaths::ProjectSavedDir() / TEXT("Extraction/Persistence");
	FParse::Value(FCommandLine::Get(), TEXT("ExtractionStore="), Directory);
	return Load(Directory);
}

#if WITH_DEV_AUTOMATION_TESTS
bool UExtractionPersistence::LoadForTest(const FString& Directory) { return Load(Directory); }
#endif

bool UExtractionPersistence::Load(const FString& Directory)
{
	Loaded = true;
	Faulted = false;
	Error.Reset();
	State = FExtractionStoreState();
	auto& Files = FPlatformFileManager::Get().GetPlatformFile();
	Journal = FPaths::ConvertRelativePathToFull(Directory / TEXT("Store.journal"));
	if (!Files.CreateDirectoryTree(*Directory))
	{
		Faulted = true;
		Error = TEXT("无法创建仓库目录");
		return false;
	}
	int64 ValidEnd = 0;
	bool Partial = false;
	if (Files.FileExists(*Journal))
	{
		TUniquePtr<IFileHandle> File(Files.OpenRead(*Journal));
		if (!File)
		{
			Faulted = true;
			Error = TEXT("无法读取仓库存档");
			return false;
		}
		while (File->Tell() < File->Size())
		{
			uint32 Header[4];
			if (File->Size() - File->Tell() < sizeof(Header))
			{
				Partial = true;
				break;
			}
			if (!File->Read(reinterpret_cast<uint8*>(Header), sizeof(Header)) || Header[0] != Magic || Header[1] != 1 ||
				Header[2] > MaxRecord)
			{
				Faulted = true;
				Error = TEXT("仓库日志头损坏，停止载入");
				return false;
			}
			if (File->Size() - File->Tell() < Header[2])
			{
				Partial = true;
				break;
			}
			TArray<uint8> Bytes;
			Bytes.SetNumUninitialized(Header[2]);
			if (!File->Read(Bytes.GetData(), Bytes.Num()) || FCrc::MemCrc32(Bytes.GetData(), Bytes.Num()) != Header[3])
			{
				Faulted = true;
				Error = TEXT("仓库日志校验失败，停止载入");
				return false;
			}
			FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
			FExtractionStoreState Next;
			if (!FJsonObjectConverter::JsonObjectStringToUStruct(FString(Text.Length(), Text.Get()), &Next, 0, 0)
				|| Next.Sequence != State.Sequence + 1 || (State.Sequence > 0 && Next.Version < State.Version) || !
				Validate(Next))
			{
				Faulted = true;
				Error = TEXT("仓库日志内容无效，停止载入");
				return false;
			}
			State = MoveTemp(Next);
			ValidEnd = File->Tell();
		}
	}
	// Back up the exact v1 journal before recovery, tail repair or migration writes.
	if (State.Sequence > 0 && State.Version == 1)
	{
		if (!PreserveLegacyJournal(Files, Journal, ValidEnd))
		{
			Faulted = true;
			Error = TEXT("无法备份旧版仓库，未执行迁移");
			return false;
		}
	}
	if (Partial)
	{
		TUniquePtr<IFileHandle> File(Files.OpenWrite(*Journal, true));
		if (!File || !File->Truncate(ValidEnd) || !File->Flush(true))
		{
			Faulted = true;
			Error = TEXT("无法修复未完成的日志尾部");
			return false;
		}
		UE_LOG(LogTemp, Display, TEXT("EXTRACTION_STORE_PARTIAL_TAIL Bytes=%lld"), ValidEnd);
	}
	if (!State.Id.IsValid())
	{
		FExtractionStoreState Initial;
		Initial.Id = FGuid::NewGuid();
		if (!Commit(Initial)) return false;
	}
	if (State.Version == 1)
	{
		FExtractionStoreState Migrated = State;
		Migrated.Version = 2;
		if (!Commit(MoveTemp(Migrated))) return false;
		UE_LOG(LogTemp, Display, TEXT("EXTRACTION_STORE_MIGRATED Version=2"));
	}
	return Recover();
}

bool UExtractionPersistence::Validate(const FExtractionStoreState& Candidate) const
{
	if ((Candidate.Version != 1 && Candidate.Version != 2) || !Candidate.Id.IsValid()) return false;
	TSet<FGuid> Profiles, Items;
	for (const auto& P : Candidate.Profiles)
	{
		if (!P.View.Profile.IsValid() || Profiles.Contains(P.View.Profile) || P.View.Store != Candidate.Id || P.
			CredentialHash.Len() != 40)
			return false;
		Profiles.Add(P.View.Profile);
		FString Why;
		if (!ExtractionItems::Validate(P.View.Stash, Why) || !ExtractionItems::Validate(P.View.Loadout, Why))
			return
				false;
		for (const auto* List : {&P.View.Stash.Items, &P.View.Loadout.Items, &P.View.Overflow})
			for (const auto& I : *List)
			{
				if (!ValidItem(I) || Items.Contains(I.Id)) return false;
				Items.Add(I.Id);
				for (const auto& Part : I.Attachments)
				{
					if (Items.Contains(Part.Id)) return false;
					Items.Add(Part.Id);
				}
			}
	}
	TSet<FGuid> Raids;
	for (const auto& R : Candidate.Raids)
	{
		if (!R.Id.IsValid() || Raids.Contains(R.Id)) return false;
		Raids.Add(R.Id);
		TSet<FGuid> Members;
		for (const auto& P : R.Players)
		{
			if (!Profiles.Contains(P.Profile) || Members.Contains(P.Profile)) return false;
			Members.Add(P.Profile);
			for (const auto& I : P.Original.Items) if (!ValidItem(I)) return false;
		}
	}
	return true;
}

bool UExtractionPersistence::Commit(FExtractionStoreState Candidate)
{
	if (Faulted) return false;
	Candidate.Sequence = State.Sequence + 1;
	if (!Validate(Candidate))
	{
		Error = TEXT("库存验证失败，未保存");
		return false;
	}
	FString Json;
	if (!FJsonObjectConverter::UStructToJsonObjectString(Candidate, Json, 0, 0, 0, nullptr, false)) return false;
	FTCHARToUTF8 Bytes(*Json);
	uint32 Header[] = {Magic, 1, static_cast<uint32>(Bytes.Length()), FCrc::MemCrc32(Bytes.Get(), Bytes.Length())};
	bool Written = false;
#if WITH_DEV_AUTOMATION_TESTS
	if (!FailNextWrite)
#endif
	{
		TUniquePtr<IFileHandle> File(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Journal, true));
		Written = File && Bytes.Length() <= MaxRecord
			&& File->Write(reinterpret_cast<const uint8*>(Header), sizeof(Header))
			&& File->Write(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length()) && File->Flush(true);
	}
	if (!Written)
	{
		Faulted = true;
		Error = TEXT("仓库存档写入失败，操作暂停；请恢复服务器存储后重启");
		UE_LOG(LogTemp, Error, TEXT("EXTRACTION_STORE_WRITE_FAILED Sequence=%lld"), Candidate.Sequence);
		return false;
	}
	State = MoveTemp(Candidate);
	Error.Reset();
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_STORE_COMMIT Sequence=%lld"), State.Sequence);
	return true;
}

const FExtractionProfileView* UExtractionPersistence::View(FGuid Id) const
{
	const auto* P = State.Profiles.FindByPredicate([&](const auto& V) { return V.View.Profile == Id; });
	return P ? &P->View : nullptr;
}

const FExtractionRaidRecord* UExtractionPersistence::FindRaid(FGuid Id) const
{
	return State.Raids.FindByPredicate([&](const auto& R) { return R.Id == Id; });
}

bool UExtractionPersistence::IsInRaid(FGuid Id) const
{
	for (const auto& R : State.Raids)
		if (!R.Closed)
			for (const auto& P : R.Players)
				if (P.Profile == Id && P.Outcome == EExtractionOutcome::None)
					return true;
	return false;
}

bool UExtractionPersistence::OpenProfile(FGuid Requested, const FString& Credential, FGuid& Id, FString& Issued)
{
	if (!EnsureLoaded()) return false;
	if (Requested.IsValid())
	{
		const auto* P = ProfileIn(State, Requested);
		if (!P || Credential.Len() != 64 || P->CredentialHash != Hash(Credential))
		{
			Error = TEXT("档案凭据无效");
			return false;
		}
		Id = Requested;
		return true;
	}
	if (!Credential.IsEmpty())
	{
		Error = TEXT("档案请求无效");
		return false;
	}
	FExtractionStoreState Candidate = State;
	FExtractionStoredProfile P;
	P.View.Profile = FGuid::NewGuid();
	P.View.Store = State.Id;
	P.View.Revision = 1;
	P.View.Stash.Id = FGuid::NewGuid();
	P.View.Stash.Width = 10;
	P.View.Stash.Height = 12;
	P.View.Loadout.Id = FGuid::NewGuid();
	P.View.Loadout.WeaponSlots = true;
	Issued = FGuid::NewGuid().ToString(EGuidFormats::Digits) + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	P.CredentialHash = Hash(Issued);
	for (int32 Kit = 0; Kit < 3; ++Kit)
	{
		ExtractionItems::AutoPlace(P.View.Stash, ExtractionItems::Make(TEXT("AK")));
		for (int32 J = 0; J < 2; ++J)
		{
			ExtractionItems::AutoPlace(P.View.Stash, ExtractionItems::Make(TEXT("Ammo"), 60));
			ExtractionItems::AutoPlace(P.View.Stash, ExtractionItems::Make(TEXT("Medkit")));
		}
	}
	Id = P.View.Profile;
	Candidate.Profiles.Add(P);
	return Commit(MoveTemp(Candidate));
}

bool UExtractionPersistence::Move(FGuid Id, const FExtractionStorageRequest& R)
{
	if (!EnsureLoaded()) return false;
	const auto* Existing = ProfileIn(State, Id);
	if (!Existing || IsInRaid(Id))
	{
		Error = TEXT("当前不能整理仓库");
		return false;
	}
	if (R.Operation.IsValid() && Existing->LastOperation == R.Operation) return true;
	if (!R.Operation.IsValid() || R.Revision != Existing->View.Revision || R.Move.Op == EExtractionItemOp::Drop || R.
		Move.Op == EExtractionItemOp::Use
		|| R.To == EExtractionStorageSide::Overflow || static_cast<uint8>(R.To) > 1 || static_cast<uint8>(R.From) > 2)
	{
		Error = TEXT("仓库已变化，请重试");
		return false;
	}
	FExtractionStoreState Candidate = State;
	auto* P = ProfileIn(Candidate, Id);
	auto& V = P->View;
	auto& Target = R.To == EExtractionStorageSide::Stash ? V.Stash : V.Loadout;
	if (R.From == EExtractionStorageSide::Overflow)
	{
		if (R.To != EExtractionStorageSide::Stash || R.Move.Op != EExtractionItemOp::Transfer)
		{
			Error = TEXT("请先领取到仓库");
			return false;
		}
		const auto* Item = V.Overflow.FindByPredicate([&](const auto& I) { return I.Id == R.Move.Item; });
		if (!Item)
		{
			Error = TEXT("物资已领取");
			return false;
		}
		FExtractionContainer From;
		From.Id = FGuid::NewGuid();
		From.Width = From.Height = 16;
		FExtractionItem Copy = *Item;
		Copy.Cell = FIntPoint::ZeroValue;
		Copy.Slot = -1;
		From.Items.Add(Copy);
		if (!ExtractionItems::Transfer(From, Target, R.Move, Error)) return false;
		V.Overflow.RemoveAll([&](const auto& I) { return I.Id == Copy.Id; });
		V.Overflow.Append(From.Items);
	}
	else
	{
		auto& From = R.From == EExtractionStorageSide::Stash ? V.Stash : V.Loadout;
		if (R.Move.Op == EExtractionItemOp::Transfer)
		{
			if (!ExtractionItems::Transfer(From, Target, R.Move, Error)) return false;
		}
		else
		{
			if (R.From != R.To)
			{
				Error = TEXT("请在同一容器内调整装备");
				return false;
			}
			if (!ExtractionItems::Configure(From, R.Move, Error)) return false;
		}
	}
	++V.Revision;
	P->LastOperation = R.Operation;
	return Commit(MoveTemp(Candidate));
}

bool UExtractionPersistence::BeginRaid(const TArray<FGuid>& Profiles, FGuid& Id)
{
	if (!EnsureLoaded() || Profiles.IsEmpty()) return false;
	FExtractionStoreState Candidate = State;
	FExtractionRaidRecord Raid;
	Raid.Id = FGuid::NewGuid();
	TSet<FGuid> Seen;
	for (FGuid Key : Profiles)
	{
		auto* P = ProfileIn(Candidate, Key);
		if (!P || IsInRaid(Key) || Seen.Contains(Key))
		{
			Error = TEXT("出战档案无效或已在对局中");
			return false;
		}
		Seen.Add(Key);
		FExtractionRaidParticipant Member;
		Member.Profile = Key;
		Member.Original = P->View.Loadout;
		for (auto& I : Member.Original.Items) ExtractionItems::SeedOrigins(I);
		Raid.Players.Add(Member);
		P->View.Loadout.Items.Reset();
		++P->View.Loadout.Revision;
		++P->View.Revision;
		P->View.Result = FExtractionResult();
	}
	Id = Raid.Id;
	Candidate.Raids.Add(Raid);
	return Commit(MoveTemp(Candidate));
}

bool UExtractionPersistence::ActivateRaid(FGuid Id)
{
	if (!EnsureLoaded()) return false;
	FExtractionStoreState Candidate = State;
	auto* R = RaidIn(Candidate, Id);
	if (!R || R->Closed) return false;
	if (R->Active) return true;
	R->Active = true;
	return Commit(MoveTemp(Candidate));
}

void UExtractionPersistence::Deposit(FExtractionProfileView& P, TArray<FExtractionItem> Items)
{
	for (auto I : Items)
	{
		I.Slot = -1;
		if (!ExtractionItems::AutoPlace(P.Stash, I)) P.Overflow.Add(I);
	}
	++P.Stash.Revision;
	++P.Revision;
}

bool UExtractionPersistence::Settle(FGuid RaidId, FGuid Profile, EExtractionOutcome Outcome,
                                    const FExtractionContainer& Inventory)
{
	if (!EnsureLoaded() || Outcome == EExtractionOutcome::None || Outcome == EExtractionOutcome::Recovered || Outcome ==
		EExtractionOutcome::Aborted)
		return false;
	FExtractionStoreState Candidate = State;
	auto* R = RaidIn(Candidate, RaidId);
	auto* P = ProfileIn(Candidate, Profile);
	auto* M = R ? R->Players.FindByPredicate([&](const auto& V) { return V.Profile == Profile; }) : nullptr;
	if (!M || !P || !R->Active)
	{
		Error = TEXT("对局尚未开始或身份无效");
		return false;
	}
	if (M->Outcome != EExtractionOutcome::None) return true;
	FString Why;
	if (!ExtractionItems::Validate(Inventory, Why))
	{
		Error = Why;
		return false;
	}
	for (const auto& I : Inventory.Items)
		if (!ValidItem(I))
		{
			Error = TEXT("物资来源无效");
			return false;
		}
	M->Outcome = Outcome;
	P->View.Result.Raid = RaidId;
	P->View.Result.Outcome = Outcome;
	P->View.Result.Items = Inventory.Items;
	if (Outcome == EExtractionOutcome::Extracted)
	{
		for (const auto& I : Inventory.Items)
		{
			ExtractionItems::MergeOrigins(R->Exported, I.Origins);
			ExtractionItems::MergeOrigins(R->Exported, I.MagazineOrigins);
			for (const auto& Part : I.Attachments) ExtractionItems::MergeOrigins(R->Exported, Part.Origins);
		}
		Deposit(P->View, Inventory.Items);
	}
	else ++P->View.Revision;
	P->View.Result.OverflowCount = P->View.Overflow.Num();
	R->Closed = !R->Players.ContainsByPredicate([](const auto& V) { return V.Outcome == EExtractionOutcome::None; });
	return Commit(MoveTemp(Candidate));
}

bool UExtractionPersistence::Recover()
{
	FExtractionStoreState Candidate = State;
	bool Changed = false;
	for (auto& R : Candidate.Raids)
	{
		if (R.Closed) continue;
		for (auto& M : R.Players)
		{
			if (M.Outcome != EExtractionOutcome::None) continue;
			auto* P = ProfileIn(Candidate, M.Profile);
			TArray<FExtractionItem> Refund;
			for (auto I : M.Original.Items)
			{
				TArray<FExtractionAttachment> RemainingParts;
				for (const auto& Part : I.Attachments)
				{
					auto Remaining = Part;
					Remaining.Origins.Reset();
					for (const auto& O : Part.Origins)
						if (const int32 N = O.Quantity - Count(R.Exported, O.Lot); N > 0)
							Remaining.Origins.Add({
								O.Lot, N
							});
					if (!Remaining.Origins.IsEmpty()) RemainingParts.Add(Remaining);
				}
				I.Attachments = RemainingParts;
				TArray<FExtractionOrigin> Body, Magazine;
				for (const auto& O : I.Origins)
					if (const int32 Remaining = O.Quantity - Count(R.Exported, O.Lot); Remaining > 0)
						Body.Add({
							O.Lot, Remaining
						});
				for (const auto& O : I.MagazineOrigins)
					if (const int32 Remaining = O.Quantity - Count(R.Exported, O.Lot); Remaining > 0)
						Magazine.Add({
							O.Lot, Remaining
						});
				I.Quantity = 0;
				for (const auto& O : Body) I.Quantity += O.Quantity;
				I.Magazine = 0;
				for (const auto& O : Magazine) I.Magazine += O.Quantity;
				I.Origins = Body;
				I.MagazineOrigins = Magazine;
				// A partial exported stack can retain the original instance GUID in another stash.
				if (I.Quantity && I.Quantity < M.Original.Find(I.Id)->Quantity) I.Id = FGuid::NewGuid();
				if (I.Quantity) Refund.Add(I);
				else
				{
					for (const auto& Part : I.Attachments) Refund.Add(ExtractionItems::Unmount(Part));
					if (I.Magazine)
					{
						auto Ammo = ExtractionItems::Make(I.MagazineAmmo, I.Magazine);
						Ammo.Origins = Magazine;
						Refund.Add(Ammo);
					}
				}
			}
			M.Outcome = R.Active ? EExtractionOutcome::Recovered : EExtractionOutcome::Aborted;
			Deposit(P->View, Refund);
			P->View.Result.Raid = R.Id;
			P->View.Result.Outcome = M.Outcome;
			P->View.Result.Items = Refund;
			P->View.Result.OverflowCount = P->View.Overflow.Num();
			Changed = true;
		}
		R.Closed = true;
	}
	return !Changed || Commit(MoveTemp(Candidate));
}

bool UExtractionPersistence::AbortRaid(FGuid Id)
{
	if (!EnsureLoaded()) return false;
	const auto* R = FindRaid(Id);
	if (!R || R->Active) return false;
	if (R->Closed) return true;
	FExtractionStoreState Candidate = State;
	AbortPreparedRecord(Candidate, *RaidIn(Candidate, Id));
	return Commit(MoveTemp(Candidate));
}

void UExtractionPersistence::AbortPreparedRecord(FExtractionStoreState& Candidate, FExtractionRaidRecord& Raid)
{
	for (auto& Member : Raid.Players)
	{
		if (Member.Outcome != EExtractionOutcome::None) continue;
		auto& View = ProfileIn(Candidate, Member.Profile)->View;
		Deposit(View, Member.Original.Items);
		Member.Outcome = EExtractionOutcome::Aborted;
		View.Result.Raid = Raid.Id;
		View.Result.Outcome = EExtractionOutcome::Aborted;
		View.Result.Items = Member.Original.Items;
		View.Result.OverflowCount = View.Overflow.Num();
	}
	Raid.Closed = true;
}

bool UExtractionPersistence::AbortPreparedRaids()
{
	if (!EnsureLoaded()) return false;
	FExtractionStoreState Candidate = State;
	bool Changed = false;
	for (auto& Raid : Candidate.Raids)
		if (!Raid.Active && !Raid.Closed)
		{
			AbortPreparedRecord(Candidate, Raid);
			Changed = true;
		}
	return !Changed || Commit(MoveTemp(Candidate));
}
