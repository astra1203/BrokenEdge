#include "ExtractionLootLayout.h"
#include "ExtractionLootActor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

const FName UExtractionLootLayout::LooseKind(TEXT("Loose"));

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
	for (int32 I : Selected)
	{
		const auto& P = Points[I];
		auto* Actor = World->SpawnActorDeferred<AExtractionLootActor>(AExtractionLootActor::StaticClass(), P.Transform,
		                                                              nullptr, nullptr,
		                                                              ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Actor) continue;
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
