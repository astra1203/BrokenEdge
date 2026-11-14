#include "BrokenEdge/Extraction/Map/ExtractionMapRegistry.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"

void UExtractionMapRegistry::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString Id;
	if (FParse::Value(FCommandLine::Get(), TEXT("ExtractionMap="), Id)) SelectedMap = FName(Id);
}

UExtractionMapDefinition* UExtractionMapRegistry::Find(FName Id) const
{
	if (Id.IsNone()) return nullptr;
	for (const auto& Ref : Maps)
		if (auto* D = Ref.LoadSynchronous(); D && D->MapId == Id) return D;
	return nullptr;
}

FString UExtractionMapRegistry::WorldPackage(const UWorld* World)
{
	if (!World) return {};
	FString Name = World->GetOutermost()->GetName();
	if (!World->StreamingLevelsPrefix.IsEmpty()) Name.ReplaceInline(*World->StreamingLevelsPrefix, TEXT(""));
	return Name;
}

UExtractionMapDefinition* UExtractionMapRegistry::FindWorld(const UWorld* World) const
{
	const FString Package = WorldPackage(World);
	for (const auto& Ref : Maps)
		if (auto* D = Ref.LoadSynchronous(); D && D->Level.ToSoftObjectPath().GetLongPackageName() == Package) return D;
	return nullptr;
}

bool UExtractionMapRegistry::Validate(FName Id, FString& Error) const
{
	const auto* D = Find(Id);
	if (!D || D->Level.IsNull() || !FPackageName::DoesPackageExist(D->Level.ToSoftObjectPath().GetLongPackageName()))
	{
		Error = TEXT("地图未注册或关卡资源缺失，无法出战");
		return false;
	}
	TSet<FName> Groups, Exits;
	for (const auto& G : D->SpawnGroups)
	{
		if (G.Id.IsNone() || Groups.Contains(G.Id) || G.Positions.Num() < 3) break;
		bool Valid = true;
		for (const auto& P : G.Positions) Valid &= !P.ContainsNaN();
		if (Valid) Groups.Add(G.Id);
	}
	for (const auto& E : D->Exits) if (E.IsValid()) Exits.Add(E.Id);
	if (Groups.Num() != D->SpawnGroups.Num() || Groups.Num() < 2 || Exits.IsEmpty() || Exits.Num() != D->Exits.Num() || !D->LootLayout)
	{
		Error = TEXT("地图出生、撤离或物资配置无效，无法出战");
		return false;
	}
	Error.Reset();
	return true;
}
