#include "ExtractionProgression.h"
// 长期成长与局内进度分离：服务器只在成功撤离时累积个人任务进度，交易和领奖由仓库事务保存。
// 本文件的 Execute 修改调用者提供的候选对象，不自行回滚或写盘；Persistence 必须在副本上调用并统一提交。

// 初始化三段等级、商品和三步任务链；Game 配置可覆盖默认调参，但仍需满足配置校验。
UExtractionProgressionRules::UExtractionProgressionRules()
{
	LevelExperience = {0, 100, 300};
	Offers = {
		{TEXT("AK"), 1200, 1}, {TEXT("Ammo"), 2, 1}, {TEXT("Medkit"), 150, 1},
		{TEXT("Bandage"), 60, 1}, {TEXT("Splint"), 80, 1}, {TEXT("Helmet2"), 400, 1},
		{TEXT("Vest2"), 600, 1}, {TEXT("Key"), 300, 1}, {TEXT("RedDot"), 300, 2},
		{TEXT("VerticalGrip"), 300, 2}, {TEXT("Compensator"), 300, 2}, {TEXT("AmmoAP"), 6, 3},
		{TEXT("Helmet3"), 800, 3}, {TEXT("Vest3"), 1200, 3},
		{TEXT("SCARH"), 1800, 1}, {TEXT("Ammo76251"), 4, 1},
		// M4A1：射速高、单发伤害低于 AK（官方 31 / 40），价位介于 AK 与 SCAR-H 之间。
		{TEXT("M4A1"), 1500, 1}, {TEXT("Ammo55645"), 3, 1},
		// 航天基地钥匙卡：商店可购买，买入后按定义补满开门次数，需在仓库移入配装才能带入。
		// 价格按房间价值与开门次数分层：入门房 3 次 < 二楼房 2 次 < 核心/总裁 1-2 次高价值。
		{TEXT("Key_Contract"), 700, 1}, {TEXT("Key_StaffPassage"), 800, 1},
		{TEXT("Key_EastBridge"), 1200, 2}, {TEXT("Key_Monitor"), 1400, 2},
		{TEXT("Key_President"), 2400, 3}, {TEXT("Key_Core"), 2600, 3},
		// 胸挂/背包/安全箱：官方价位（胸包 1 万、ALS 2.8 万、安全箱数百万）与本项目经济不同量级，
		// 按格数分档换算到项目价位；基础安全箱新档案自带，不在商店出售。
		{TEXT("Rig_Pouch"), 300, 1}, {TEXT("Rig_Ammo"), 500, 1}, {TEXT("Rig_Assault"), 800, 2},
		{TEXT("Rig_Hurricane"), 1200, 3},
		{TEXT("Bag_Climb"), 300, 1}, {TEXT("Bag_Hike"), 500, 1}, {TEXT("Bag_ALS"), 900, 2},
		{TEXT("Bag_GTO"), 1600, 3},
		{TEXT("Safe_Advanced"), 3000, 2}, {TEXT("Safe_Premium"), 6000, 3}
	};
	Quests = {
		{TEXT("初次撤离"), TEXT("接取后成功撤离一次"), 1, 500, 100},
		{TEXT("清理守卫"), TEXT("本人击杀两名守卫，成功撤离才保留本局进度"), 2, 1000, 200},
		{TEXT("机密回收"), TEXT("接取后带出钥匙房文件，在仓库提交一份"), 1, 1500, 300}
	};
}

// 按定义 ID 查找商品，返回空表示不在当前商品表中。
const FExtractionOffer* UExtractionProgressionRules::Offer(FName Id) const
{
	return Offers.FindByPredicate([&](const auto& O) { return O.Definition == Id; });
}

// 按累计经验阈值计算等级，经验不在升级时扣除。
int32 UExtractionProgressionRules::Level(int64 XP) const
{
	int32 Result = 1;
	for (int32 I = 1; I < LevelExperience.Num(); ++I) if (XP >= LevelExperience[I]) Result = I + 1;
	return Result;
}

// 检查三等级三任务结构、单价与奖励范围、定义白名单和唯一商品；任务文件不允许作为商品。
bool UExtractionProgressionRules::IsValidConfiguration() const
{
	if (StartingCredits < 0 || Quests.Num() != 3 || LevelExperience.Num() != 3 || LevelExperience[0] != 0
		|| LevelExperience[1] <= 0 || LevelExperience[2] <= LevelExperience[1]
		|| ElectronicsSellPrice < 0 || ToolsSellPrice < 0)
		return false;
	TSet<FName> Seen;
	for (const auto& O : Offers)
	{
		const auto* Definition = UExtractionItemDefinition::Get(O.Definition);
		if (!Definition || Definition->MaxStack < 1 || Seen.Contains(O.Definition) || O.Price <= 0
			|| O.Price > 1000000 || O.Level < 1 || O.Level > 3 || O.Definition == TEXT("Document"))
			return false;
		Seen.Add(O.Definition);
	}
	for (const auto& Q : Quests)
		if (Q.Target <= 0 || Q.Target > 1000 || Q.Credits < 0
			|| Q.Experience < 0 || Q.Credits > 1000000 || Q.Experience > 1000000)
			return false;
	return true;
}

// 计算指定数量的回收报价；护甲按耐久折价，整枪包括弹匣和附件，负数表示不允许出售。
int64 UExtractionProgressionRules::SellPrice(const FExtractionItem& I, int32 Quantity) const
{
	if (Quantity <= 0 || Quantity > I.Quantity) return -1;
	if (I.Definition == TEXT("Electronics")) return int64(ElectronicsSellPrice) * Quantity;
	if (I.Definition == TEXT("Tools")) return int64(ToolsSellPrice) * Quantity;
	const auto* O = Offer(I.Definition);
	const auto* D = UExtractionItemDefinition::Get(I.Definition);
	if (!O || !D) return -1;
	int64 Value = int64(O->Price / 2) * Quantity;
	if (D->Kind == EExtractionItemKind::Armor)
		Value = FMath::FloorToInt64(
			double(O->Price) * .5 * FMath::Clamp(double(I.Durability) / double(D->MaxDurability), 0., 1.));
	if (D->Kind == EExtractionItemKind::Weapon)
	{
		const auto* Ammo = Offer(I.MagazineAmmo);
		if (!Ammo && I.Magazine) return -1;
		Value += Ammo ? int64(Ammo->Price / 2) * I.Magazine : 0;
		for (const auto& Part : I.Attachments)
		{
			const auto* A = Offer(Part.Definition);
			if (!A) return -1;
			Value += A->Price / 2;
		}
	}
	return Value;
}

// 新档案或版本迁移时设置初始货币、经验和三个任务槽，不是每次进入大厅都重置。
void FExtractionProgression::Initialize()
{
	Credits = GetDefault<UExtractionProgressionRules>()->StartingCredits;
	Experience = 0;
	Quests.SetNum(3);
}

// 校验余额经验范围和任务前置关系，保证未接任务不含进度、文件资格不重复。
bool FExtractionProgression::Validate() const
{
	if (Credits < 0 || Credits > 1000000000000LL || Experience < 0 || Experience > 1000000000LL || Quests.Num() != 3)
		return false;
	for (int32 I = 0; I < Quests.Num(); ++I)
	{
		const auto& Q = Quests[I];
		if (Q.Progress < 0 || Q.Progress > 1000 || (Q.Claimed && !Q.Accepted)
			|| (I > 0 && Q.Accepted && !Quests[I - 1].Claimed)
			|| (!Q.Accepted && (Q.Progress || Q.Delivered || !Q.EligibleFiles.IsEmpty()))
			|| (I != 2 && (Q.Delivered || !Q.EligibleFiles.IsEmpty())))
			return false;
		TSet<FGuid> Lots;
		for (FGuid Lot : Q.EligibleFiles)
		{
			if (!Lot.IsValid() || Lots.Contains(Lot)) return false;
			Lots.Add(Lot);
		}
	}
	return true;
}

// 把请求版本、操作类型及参数编码成回执签名；相同操作 ID 携带不同内容必须被拒绝。
FString FExtractionEconomyRequest::Signature() const
{
	return FString::Printf(TEXT("%d:%d:%s:%s:%d:%d"), Revision, int32(Action), *Definition.ToString(),
	                       *Item.ToString(EGuidFormats::Digits), Quantity, Quest);
}

// 处理购买、出售、接任务、提交文件和领奖；返回 false 时调用者丢弃整个候选状态。
// 购买分堆放入且枪械为空弹匣，出售同时扣来源，任务文件必须匹配本人接取后成功带出的批次。
bool ExtractionProgression::Execute(FExtractionProgression& P, FExtractionContainer& Stash,
                                    const FExtractionEconomyRequest& R, FString& Error)
{
	const auto* Rules = GetDefault<UExtractionProgressionRules>();
	auto Reject = [&](const TCHAR* Text)
	{
		Error = Text;
		return false;
	};
	if (!P.Validate() || !Rules->IsValidConfiguration()) return Reject(TEXT("成长配置或档案无效"));
	if (R.Quantity < 1 || R.Quantity > 600) return Reject(TEXT("数量须为 1 至 600"));
	if (R.Action == EExtractionEconomyAction::Buy)
	{
		const auto* O = Rules->Offer(R.Definition);
		const auto* D = UExtractionItemDefinition::Get(R.Definition);
		if (!O || !D || R.Quest != INDEX_NONE || R.Item.IsValid()) return Reject(TEXT("商品请求无效"));
		if (Rules->Level(P.Experience) < O->Level) return Reject(TEXT("等级不足，商品未解锁"));
		// 用 64 位金额计算总价；最终提交发生在外层持久化事务，不在此逐件落盘。
		const int64 Cost = int64(O->Price) * R.Quantity;
		if (P.Credits < Cost) return Reject(TEXT("余额不足"));
		for (int32 Remaining = R.Quantity; Remaining > 0;)
		{
			const int32 N = FMath::Min(Remaining, D->MaxStack);
			auto I = ExtractionItems::Make(R.Definition, N);
			if (D->Kind == EExtractionItemKind::Weapon)
			{
				I.Magazine = 0;
				I.MagazineOrigins.Reset();
			}
			if (!ExtractionItems::AutoPlace(Stash, I)) return Reject(TEXT("仓库空间不足，整笔购买已取消"));
			Remaining -= N;
		}
		P.Credits -= Cost;
		++Stash.Revision;
	}
	else if (R.Action == EExtractionEconomyAction::Sell)
	{
		auto* I = Stash.Find(R.Item);
		if (!I || R.Quest != INDEX_NONE || !R.Definition.IsNone()) return Reject(TEXT("请选择仓库中的物品"));
		const int64 Price = Rules->SellPrice(*I, R.Quantity);
		if (Price < 0) return Reject(TEXT("此物品不可出售或数量无效"));
		P.Credits += Price;
		ExtractionItems::TakeOrigins(I->Origins, R.Quantity);
		I->Quantity -= R.Quantity;
		if (!I->Quantity) Stash.Items.RemoveAll([&](const auto& V) { return V.Id == R.Item; });
		++Stash.Revision;
	}
	else
	{
		if (!Rules->Quests.IsValidIndex(R.Quest) || R.Quantity != 1 || !R.Definition.IsNone() || R.Item.IsValid())
			return Reject(TEXT("任务请求无效"));
		auto& Q = P.Quests[R.Quest];
		const auto& D = Rules->Quests[R.Quest];
		if (R.Action == EExtractionEconomyAction::AcceptQuest)
		{
			if (Q.Accepted || (R.Quest > 0 && !P.Quests[R.Quest - 1].Claimed) || Rules->Level(P.Experience) < R.Quest +
				1)
				return Reject(TEXT("任务已接取或前置条件未满足"));
			Q.Accepted = true;
		}
		else if (R.Action == EExtractionEconomyAction::DeliverQuest)
		{
			// 只有第三步文件任务接受物品提交，已提交或已领奖不能再次消耗文件。
			if (R.Quest != 2 || !Q.Accepted || Q.Claimed || Q.Delivered) return Reject(TEXT("当前任务无需提交物品"));
			const int32 Index = Stash.Items.IndexOfByPredicate([&](const auto& I)
			{
				return I.Definition == TEXT("Document") && I.Origins.ContainsByPredicate([&](const auto& O)
				{
					return Q.EligibleFiles.Contains(O.Lot);
				});
			});
			if (Index == INDEX_NONE) return Reject(TEXT("仓库中没有本人在接取后成功带出的钥匙房文件"));
			Stash.Items.RemoveAt(Index);
			++Stash.Revision;
			Q.Delivered = true;
			Q.Progress = 1;
			Q.EligibleFiles.Reset();
		}
		else if (R.Action == EExtractionEconomyAction::ClaimQuest)
		{
			if (!Q.Accepted || Q.Claimed || Q.Progress < D.Target || (R.Quest == 2 && !Q.Delivered))
				return Reject(TEXT("任务尚未完成或奖励已领取"));
			P.Credits += D.Credits;
			P.Experience += D.Experience;
			Q.Claimed = true;
		}
		else return Reject(TEXT("未知操作"));
	}
	return P.Validate() || Reject(TEXT("余额或经验超出允许范围"));
}

// 只由成功撤离结算调用：累计本人守卫击杀，并登记本局钥匙房文件批次，不替其他队员共享任务。
void ExtractionProgression::Extract(FExtractionProgression& P, const FExtractionContainer& Inventory,
                                    const FExtractionRaidProgress& Raid)
{
	const auto* Rules = GetDefault<UExtractionProgressionRules>();
	if (P.Quests.Num() != 3) return;
	if (P.Quests[0].Accepted && !P.Quests[0].Claimed) P.Quests[0].Progress = 1;
	if (P.Quests[1].Accepted && !P.Quests[1].Claimed)
		P.Quests[1].Progress = FMath::Min(Rules->Quests[1].Target,
		                                  P.Quests[1].Progress + FMath::Clamp(Raid.GuardKills, 0, 1000));
	auto& Files = P.Quests[2];
	if (Files.Accepted && !Files.Claimed && !Files.Delivered)
		for (const auto& I : Inventory.Items)
			if (I.Definition == TEXT("Document"))
				for (const auto& O : I.Origins) if (Raid.FileLots.Contains(O.Lot)) Files.EligibleFiles.AddUnique(O.Lot);
}

// 生成货币、等级和经验摘要；展示文本不参与金额或等级判定。
FString ExtractionProgression::Summary(const FExtractionProgression& P)
{
	const auto* R = GetDefault<UExtractionProgressionRules>();
	return FString::Printf(TEXT("货币 %lld  ·  等级 %d / 3  ·  经验 %lld"), P.Credits, R->Level(P.Experience), P.Experience);
}
