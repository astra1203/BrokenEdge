#include "ExtractionPersistence.h"
#include "JsonObjectConverter.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Misc/Crc.h"
// 服务器仓库的单写入者：每条日志保存完整候选状态，包含受影响档案、出战记录和带出批次数量。
// 先校验候选副本，再追加带长度与 CRC 的记录并 Flush，成功后才替换内存 State。
// GameInstanceSubsystem 跨关卡存活；它不是可复制 Actor，客户端通过 AccountComponent 获取自己的快照。

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

	// 把出战配置变成携行容器：缺背包时装上 24 格野战徒步背包（6 列 × 4 行，与旧固定网格完全一致，
	// 原有物品摆放不变），缺安全箱时装上基础安全箱。新建档案与旧档案迁移共用。
	void EquipDefaultGear(FExtractionContainer& Loadout)
	{
		Loadout.GearGrids = true;
		for (const auto& [Slot, Definition] : {TPair<int32, FName>(ExtractionGear::BagSlot, TEXT("Bag_Hike")),
		                                       TPair<int32, FName>(ExtractionGear::SafeSlot, TEXT("Safe_Basic"))})
		{
			if (Loadout.InSlot(Slot)) continue;
			auto Gear = ExtractionItems::Make(Definition);
			Gear.Slot = Slot;
			Loadout.Items.Add(Gear);
		}
		++Loadout.Revision;
	}

	// 胸挂/背包/安全箱本体直接装回出战配置（槽空时），下一局不用重新装；返回装不回去的其余物品。
	TArray<FExtractionItem> ReturnGear(FExtractionContainer& Loadout, const TArray<FExtractionItem>& Items)
	{
		TArray<FExtractionItem> Rest;
		Loadout.GearGrids = true;
		for (const auto& I : Items)
		{
			if (I.Slot >= ExtractionGear::RigSlot && I.Slot <= ExtractionGear::SafeSlot
				&& ExtractionItems::Fits(Loadout, I))
			{
				Loadout.Items.Add(I);
				++Loadout.Revision;
				continue;
			}
			Rest.Add(I);
		}
		return Rest;
	}

	bool PreserveLegacyJournal(IPlatformFile& Files, const FString& Journal, int64 ValidEnd, int32 Version)
	{
		const FString Backup = Journal + FString::Printf(TEXT(".v%d.bak"), Version);
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

// 将持久化结果转换为展示文本；未知或 None 返回空，不改变结算状态。
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

// 按需加载仓库；训练地图使用独立随机目录，避免测试消耗正式档案。
bool UExtractionPersistence::EnsureLoaded()
{
	const bool Training = GetWorld() && GetWorld()->GetMapName().Contains(TEXT("L_WeaponRange"));
	if (Loaded && Training == TrainingStore) return !Faulted;
	FString Directory = FPaths::ProjectSavedDir() / TEXT("Extraction/Persistence");
	FParse::Value(FCommandLine::Get(), TEXT("ExtractionStore="), Directory);
	if (Training)
		Directory = FPaths::ProjectSavedDir() / TEXT("Extraction/Training") / FGuid::NewGuid().ToString();
	TrainingStore = Training;
	return Load(Directory);
}

#if WITH_DEV_AUTOMATION_TESTS
// 自动化测试专用入口，显式指定隔离目录，避免访问玩家仓库。
bool UExtractionPersistence::LoadForTest(const FString& Directory) { return Load(Directory); }
#endif

// 顺序读取日志，检查帧头、长度、CRC、序号和内容；完整损坏记录直接报错，不静默丢弃。
// 只有末尾未写完整的记录可截断；旧版先保留原始备份，再迁移并恢复未结算对局。
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
	// Preserve exact legacy bytes before tail repair, recovery, or migration.
	if (State.Sequence > 0 && State.Version < 4)
	{
		if (!PreserveLegacyJournal(Files, Journal, ValidEnd, State.Version))
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
	if (State.Version < 3)
	{
		FExtractionStoreState Migrated = State;
		Migrated.Version = 3;
		for (auto& Profile : Migrated.Profiles) Profile.View.Progression.Initialize();
		if (!Commit(MoveTemp(Migrated))) return false;
		UE_LOG(LogTemp, Display, TEXT("EXTRACTION_STORE_MIGRATED Version=3"));
	}
	if (State.Version < 4)
	{
		FExtractionStoreState Migrated = State;
		Migrated.Version = 4;
		for (auto& Profile : Migrated.Profiles)
		{
			EquipDefaultGear(Profile.View.Loadout);
			++Profile.View.Revision;
		}
		if (!Commit(MoveTemp(Migrated))) return false;
		UE_LOG(LogTemp, Display, TEXT("EXTRACTION_STORE_MIGRATED Version=4"));
	}
	return Recover();
}

// 检查存储版本、档案与出战身份唯一性、容器合法性及物品来源；不接受客户端自行拼出的结果状态。
bool UExtractionPersistence::Validate(const FExtractionStoreState& Candidate) const
{
	if (Candidate.Version < 1 || Candidate.Version > 4 || !Candidate.Id.IsValid()) return false;
	TSet<FGuid> Profiles, Items;
	for (const auto& P : Candidate.Profiles)
	{
		if (!P.View.Profile.IsValid() || Profiles.Contains(P.View.Profile) || P.View.Store != Candidate.Id || P.
			CredentialHash.Len() != 40)
			return false;
		Profiles.Add(P.View.Profile);
		if (Candidate.Version >= 3 && !P.View.Progression.Validate()) return false;
		TSet<FGuid> Operations;
		for (const auto& Receipt : P.EconomyReceipts)
		{
			if (!Receipt.Operation.IsValid() || Operations.Contains(Receipt.Operation) || Receipt.Signature.IsEmpty())
				return false;
			Operations.Add(Receipt.Operation);
		}
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

// 按值接收候选快照，成功刷盘后才发布为当前 State；写入失败锁定 Faulted，需恢复存储并重启。
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
	// 只有完整记录刷盘成功才更新权威内存快照，不能先让 UI 看见尚未保存的数据。
	State = MoveTemp(Candidate);
	Error.Reset();
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_STORE_COMMIT Sequence=%lld"), State.Sequence);
	return true;
}

// 返回当前内存状态中的档案只读指针；找不到返回空，后续 Commit 替换状态后不能继续持有旧指针。
const FExtractionProfileView* UExtractionPersistence::View(FGuid Id) const
{
	const auto* P = State.Profiles.FindByPredicate([&](const auto& V) { return V.View.Profile == Id; });
	return P ? &P->View : nullptr;
}

// 按出战 ID 查找记录；返回值指向内部状态，调用方应在当前操作内使用。
const FExtractionRaidRecord* UExtractionPersistence::FindRaid(FGuid Id) const
{
	return State.Raids.FindByPredicate([&](const auto& R) { return R.Id == Id; });
}

// 未关闭记录中仍未结算的成员视为正在出战，用于阻止同时整理仓库或重复入局。
bool UExtractionPersistence::IsInRaid(FGuid Id) const
{
	for (const auto& R : State.Raids)
		if (!R.Closed)
			for (const auto& P : R.Players)
				if (P.Profile == Id && P.Outcome == EExtractionOutcome::None)
					return true;
	return false;
}

// 已有档案必须校验凭据哈希；空身份请求才创建新档案，并通过输出参数返回新凭据。
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
	P.View.Progression.Initialize();
	P.View.Stash.Id = FGuid::NewGuid();
	P.View.Stash.Width = 10;
	P.View.Stash.Height = 12;
	P.View.Loadout.Id = FGuid::NewGuid();
	P.View.Loadout.WeaponSlots = true;
	EquipDefaultGear(P.View.Loadout);
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

// 先按 Operation 与请求签名识别重试，再检查版本和出战锁；成功交易与回执一起持久化。
bool UExtractionPersistence::Economy(FGuid Id, const FExtractionEconomyRequest& R)
{
	if (!EnsureLoaded()) return false;
	const auto* Existing = ProfileIn(State, Id);
	if (!Existing || !R.Operation.IsValid())
	{
		Error = TEXT("档案或操作标识无效");
		return false;
	}
	if (const auto* Receipt = Existing->EconomyReceipts.FindByPredicate([&](const auto& V)
	{
		return V.Operation == R.Operation;
	}))
	{
		const bool Same = Receipt->Signature == R.Signature();
		Error = Same ? FString() : TEXT("重复操作标识携带不同请求");
		return Same;
	}
	if (IsInRaid(Id) || R.Revision != Existing->View.Revision)
	{
		Error = TEXT("档案已出战或已更新，请刷新后重试");
		return false;
	}
	FExtractionStoreState Candidate = State;
	auto* P = ProfileIn(Candidate, Id);
	if (!ExtractionProgression::Execute(P->View.Progression, P->View.Stash, R, Error)) return false;
	P->EconomyReceipts.Add({R.Operation, R.Signature()});
	++P->View.Revision;
	return Commit(MoveTemp(Candidate));
}

// 仓库整理只在候选副本上执行；校验档案版本、操作 ID 和来源/目标，溢出物资只能先领取到仓库。
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

// 为所有成员原子准备出战：保存携带装备原件、重新标记来源批次并清空大厅携行栏。
// 返回的 Raid ID 是待激活票据，此处尚未开始战斗。
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

// 将已准备记录标记为活动并持久化；重复激活同一活动记录视为成功。
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

// 将物品尽量自动放入仓库，放不下则放入 Overflow；这是内存修改，调用方负责最终 Commit。
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

// 校验活动记录、成员和背包；已结算成员直接返回成功，避免网络重试重复发放。
// 只有 Extracted 才入库和积累任务，其他结果记录损失；结果与带出批次在同一事务中提交。
bool UExtractionPersistence::Settle(FGuid RaidId, FGuid Profile, EExtractionOutcome Outcome,
                                    const FExtractionContainer& Inventory, const FExtractionRaidProgress& Progress)
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
	P->View.Result.ProgressMessage = TEXT("本局未增加任务进度");
	if (Outcome == EExtractionOutcome::Extracted)
	{
		ExtractionProgression::Extract(P->View.Progression, Inventory, Progress);
		P->View.Result.ProgressMessage = TEXT("成功撤离 · 本局任务进度已保存，返回大厅查看任务");
		const auto* Rules = GetDefault<UExtractionProgressionRules>();
		for (int32 Index = 0; Index < P->View.Progression.Quests.Num(); ++Index)
		{
			const auto& Quest = P->View.Progression.Quests[Index];
			if (Quest.Accepted && !Quest.Claimed)
				P->View.Result.ProgressMessage += Index == 2
					                                  ? FString::Printf(
						                                  TEXT("\n%s：已带出 %d 份合格文件，回大厅提交"), *Rules->Quests[Index].Label,
						                                  Quest.EligibleFiles.Num())
					                                  : FString::Printf(
						                                  TEXT("\n%s：%d / %d"), *Rules->Quests[Index].Label,
						                                  Quest.Progress, Rules->Quests[Index].Target);
		}
	}
	// 撤离带出全部物资；其他结局只保留安全箱及其中物品（死亡不掉落）。
	TArray<FExtractionItem> Kept;
	for (const auto& I : Inventory.Items)
		if (Outcome == EExtractionOutcome::Extracted || ExtractionItems::Protected(I)) Kept.Add(I);
	if (Outcome != EExtractionOutcome::Extracted && !Kept.IsEmpty())
		P->View.Result.ProgressMessage += TEXT("\n安全箱内物资已保留");
	// 带出的批次计入 Exported，崩溃恢复时不会再按入局原件重复返还。
	for (const auto& I : Kept)
	{
		ExtractionItems::MergeOrigins(R->Exported, I.Origins);
		ExtractionItems::MergeOrigins(R->Exported, I.MagazineOrigins);
		for (const auto& Part : I.Attachments) ExtractionItems::MergeOrigins(R->Exported, Part.Origins);
	}
	// 携行装备本体装回出战配置，其余物资入库；装不回去（槽已占用）时一起入库。
	if (!Kept.IsEmpty()) Deposit(P->View, ReturnGear(P->View.Loadout, Kept));
	else ++P->View.Revision;
	P->View.Result.OverflowCount = P->View.Overflow.Num();
	R->Closed = !R->Players.ContainsByPredicate([](const auto& V) { return V.Outcome == EExtractionOutcome::None; });
	return Commit(MoveTemp(Candidate));
}

// 重启时处理未关闭对局：从入局原物资扣除已被任何成员带出的来源批次，只返还剩余部分。
// 数量拆分后必要时更换实例 ID，避免与已经进入其他仓库的物品身份冲突。
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
			Deposit(P->View, ReturnGear(P->View.Loadout, Refund));
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

// 仅取消尚未激活的出战记录；已经关闭视为成功，活动对局不能走准备取消路径。
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

// 在候选状态中返还尚未结算成员的原装备并标记 Aborted；此辅助函数本身不刷盘。
void UExtractionPersistence::AbortPreparedRecord(FExtractionStoreState& Candidate, FExtractionRaidRecord& Raid)
{
	for (auto& Member : Raid.Players)
	{
		if (Member.Outcome != EExtractionOutcome::None) continue;
		auto& View = ProfileIn(Candidate, Member.Profile)->View;
		Deposit(View, ReturnGear(View.Loadout, Member.Original.Items));
		Member.Outcome = EExtractionOutcome::Aborted;
		View.Result.Raid = Raid.Id;
		View.Result.Outcome = EExtractionOutcome::Aborted;
		View.Result.Items = Member.Original.Items;
		View.Result.OverflowCount = View.Overflow.Num();
	}
	Raid.Closed = true;
}

// 批量取消所有未激活且未关闭记录，供加载失败或开局中止使用；统一提交一次。
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
