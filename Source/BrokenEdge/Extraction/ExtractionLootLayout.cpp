#include "ExtractionLootLayout.h"
#include "ExtractionLootActor.h"
#include "ExtractionItems.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
// 布局记录点位来源，Recipe 记录项目掉落内容，两者分离以免把兼容调参误称为源游戏规则。
// 服务器开局按固定种子选择点位；组名为空的固定点全选，有组名的候选按权重每组选一个。

const FName UExtractionLootLayout::LooseKind(TEXT("Loose"));

TArray<FName> UExtractionLootRecipe::RollPool(FRandomStream& Random) const
{
	TArray<FName> Result;
	if (PoolRolls.Y <= 0) return Result;
	TArray<FName> Candidates;
	TArray<float> Weights;
	float Total = 0;
	for (const FName Id : UExtractionItemDefinition::Ids())
	{
		const auto* D = UExtractionItemDefinition::Get(Id);
		if (!D || D->Kind != EExtractionItemKind::Valuable || D->SellValue <= 0
			|| (!PoolCategories.IsEmpty() && !PoolCategories.Contains(D->DisplayCategory)))
			continue;
		const float W = QualityWeights.IsValidIndex(D->DisplayRarity) ? QualityWeights[D->DisplayRarity] : 0.f;
		if (W <= 0) continue;
		Candidates.Add(Id);
		Weights.Add(W);
		Total += W;
	}
	if (Candidates.IsEmpty()) return Result;
	const int32 Count = Random.RandRange(FMath::Max(0, PoolRolls.X), FMath::Max(PoolRolls.X, PoolRolls.Y));
	for (int32 N = 0; N < Count; ++N)
	{
		float Pick = Random.FRandRange(0, Total);
		int32 I = 0;
		while (I < Weights.Num() - 1 && Pick >= Weights[I]) Pick -= Weights[I++];
		Result.Add(Candidates[I]);
	}
	return Result;
}

// 校验唯一 ID、变换和权重后，对组名及点位排序再按种子抽取；排序避免容器遍历顺序影响重现。
// 输出是 Points 索引，失败时清空选择并返回错误文本。
bool UExtractionLootLayout::Select(int32 Seed, TArray<int32>& Out, FString& Error) const
{
	Out.Reset();
	Error.Reset();
	TSet<FName> Ids;
	TMap<FName, TArray<int32>> Groups;
	for (int32 I = 0; I < Points.Num(); ++I)
	{
		const auto& P = Points[I];
		if (P.Id.IsNone() || Ids.Contains(P.Id) || P.Transform.ContainsNaN()
			|| !FMath::IsFinite(P.Weight) || P.Weight <= 0)
		{
			Error = FString::Printf(TEXT("Invalid loot point %s"), *P.Id.ToString());
			Out.Reset();
			return false;
		}
		Ids.Add(P.Id);
		if (!P.Enabled) continue;
		// Loose sites are drawn by SpawnLoose: they must never occupy an authored container slot.
		if (P.Kind == LooseKind) continue;
		if (P.Group.IsNone()) Out.Add(I);
		else Groups.FindOrAdd(P.Group).Add(I);
	}
	TArray<FName> Names;
	Groups.GetKeys(Names);
	Names.Sort(FNameLexicalLess());
	FRandomStream Random(Seed);
	for (FName Name : Names)
	{
		auto& Candidates = Groups[Name];
		Candidates.Sort([&](int32 A, int32 B) { return Points[A].Id.LexicalLess(Points[B].Id); });
		double Sum = 0;
		for (int32 I : Candidates) Sum += Points[I].Weight;
		double Roll = Random.GetFraction() * Sum;
		int32 Pick = Candidates.Last();
		for (int32 I : Candidates)
		{
			Roll -= Points[I].Weight;
			if (Roll < 0)
			{
				Pick = I;
				break;
			}
		}
		Out.Add(Pick);
	}
	return true;
}

// 只在服务器生成选中的容器，沿用配置高程并关闭默认落地/种子逻辑；配方异常记录警告。
void UExtractionLootLayout::Spawn(UWorld* World, int32 Seed) const
{
	if (!World || World->GetNetMode() == NM_Client) return;
	TArray<int32> Selected;
	FString Error;
	if (!Select(Seed, Selected, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("EXTRACTION_LOOT_LAYOUT_INVALID %s"), *Error);
		return;
	}
	const auto LootClass = AExtractionLootActor::ResolveSpawnClass(World);
	if (!LootClass) return;
	for (int32 I : Selected)
	{
		const auto& P = Points[I];
		auto* Actor = World->SpawnActorDeferred<AExtractionLootActor>(LootClass, P.Transform,
		                                                              nullptr, nullptr,
		                                                              ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Actor) continue;
		// 保留配置点位的楼层与高度，不执行默认向下射线或额外填充。
		Actor->SeedContents = false; // Authored floor/elevation must not be replaced by a downward trace.
		Actor->SourcePointId = P.Id;
		Actor->ContainerAppearance = P.Appearance;
		Actor->Label = P.Label.ToString();
		FExtractionContainer Contents;
		Contents.Id = FGuid::NewGuid();
		if (P.Recipe)
			for (const auto& Stack : P.Recipe->Items)
			{
				if (Stack.Quantity <= 0 || !UExtractionItemDefinition::Get(Stack.Item)
					|| !ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(Stack.Item, Stack.Quantity)))
					UE_LOG(LogTemp, Warning, TEXT("EXTRACTION_LOOT_RECIPE_INVALID point=%s item=%s"),
						*P.Id.ToString(), *Stack.Item.ToString());
			}
		if (P.Recipe)
		{
			// 每个点位独立的随机流：同一种子可复现，不同点位不会抽到同一序列。
			FRandomStream PoolRandom(Seed ^ int32(GetTypeHash(P.Id)));
			for (const FName Id : P.Recipe->RollPool(PoolRandom))
				if (!ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(Id)))
					UE_LOG(LogTemp, Display, TEXT("EXTRACTION_LOOT_POOL_FULL point=%s item=%s"), *P.Id.ToString(),
					       *Id.ToString());
		}
		Actor->SetContents(Contents);
		UGameplayStatics::FinishSpawningActor(Actor, P.Transform);
		UE_LOG(LogTemp, Display, TEXT("EXTRACTION_LOOT_SELECTED seed=%d variant=%s group=%s point=%s"),
		       Seed, *Variant.ToString(), *P.Group.ToString(), *P.Id.ToString());
	}
	SpawnLoose(World, Seed);
}

void UExtractionLootLayout::SpawnLoose(UWorld* World, int32 Seed) const
{
	TArray<int32> Candidates;
	for (int32 I = 0; I < Points.Num(); ++I)
		if (Points[I].Enabled && Points[I].Kind == LooseKind) Candidates.Add(I);
	if (Candidates.IsEmpty() || LooseSpawnCount <= 0) return;

	FRandomStream Random(Seed ^ 0x1F123BB5);
	for (int32 I = Candidates.Num() - 1; I > 0; --I)
		Candidates.Swap(I, Random.RandRange(0, I));
	const int32 Count = FMath::Min(LooseSpawnCount, Candidates.Num());
	int32 Placed = 0;

	for (int32 N = 0; N < Count; ++N)
	{
		const auto& P = Points[Candidates[N]];
		// 有收集品池的散落点一半概率改放一件随机收集品。
		if (P.Recipe && P.Recipe->PoolRolls.Y > 0 && (P.Recipe->Items.IsEmpty() || Random.FRand() < .5f))
		{
			const TArray<FName> Pool = P.Recipe->RollPool(Random);
			if (!Pool.IsEmpty())
			{
				if (auto* Actor = AExtractionLootActor::SpawnContainer(World, P.Transform.GetLocation(), true, Pool[0]))
				{
					FExtractionContainer Contents;
					Contents.Id = FGuid::NewGuid();
					if (ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(Pool[0])))
					{
						Actor->SetContents(Contents);
						Actor->SetLifeSpan(0);
						++Placed;
					}
					else Actor->Destroy();
				}
				continue;
			}
		}
		if (!P.Recipe || P.Recipe->Items.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("EXTRACTION_LOOT_LOOSE_INVALID point=%s recipe empty"),
			       *P.Id.ToString());
			continue;
		}
		const auto& Stack = P.Recipe->Items[Random.RandRange(0, P.Recipe->Items.Num() - 1)];
		if (Stack.Quantity <= 0 || !UExtractionItemDefinition::Get(Stack.Item))
		{
			UE_LOG(LogTemp, Warning, TEXT("EXTRACTION_LOOT_LOOSE_INVALID point=%s item=%s"),
			       *P.Id.ToString(), *Stack.Item.ToString());
			continue;
		}
		// Only X/Y move: the authored Z is the ground the site was validated against.
		const FVector Position = P.Transform.GetLocation();
		auto* Actor = AExtractionLootActor::SpawnContainer(World, Position, true, Stack.Item);
		if (!Actor) continue;
		FExtractionContainer Contents;
		Contents.Id = FGuid::NewGuid();
		if (!ExtractionItems::AutoPlace(Contents, ExtractionItems::Make(Stack.Item, Stack.Quantity)))
		{
			UE_LOG(LogTemp, Warning, TEXT("EXTRACTION_LOOT_LOOSE_INVALID point=%s item=%s did not fit"),
			       *P.Id.ToString(), *Stack.Item.ToString());
			Actor->Destroy();
			continue;
		}
		// SpawnContainer parks an empty loose actor on a 0.5s lifespan; content must land now.
		Actor->SetContents(Contents);
		Actor->SetLifeSpan(0);
		++Placed;
		UE_LOG(LogTemp, Display, TEXT("EXTRACTION_LOOT_LOOSE seed=%d point=%s item=%s x%d"),
		       Seed, *P.Id.ToString(), *Stack.Item.ToString(), Stack.Quantity);
	}
	UE_LOG(LogTemp, Display, TEXT("EXTRACTION_LOOT_LOOSE_SUMMARY seed=%d candidates=%d placed=%d"),
	       Seed, Candidates.Num(), Placed);
}
