#include "ExtractionLootLayout.h"
#include "ExtractionLootActor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

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
}
