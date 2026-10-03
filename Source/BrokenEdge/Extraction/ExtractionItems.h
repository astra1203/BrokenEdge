#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ExtractionItems.generated.h"

UENUM(BlueprintType)
enum class EExtractionItemKind : uint8 { Weapon, Ammo, Medical, Valuable };

/** Stable definitions are cooked assets; instances contain only match state. */
UCLASS(BlueprintType)
class BROKENEDGE_API UExtractionItemDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName ItemId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText Label;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EExtractionItemKind Kind = EExtractionItemKind::Valuable;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FIntPoint Size = FIntPoint(1, 1);
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 MaxStack = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float UseSeconds = 3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float HealAmount = 50;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<class AWeapon> WeaponClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UStaticMesh> WorldMesh;
	static const UExtractionItemDefinition* Get(FName Id);
	static const TArray<FName>& Ids();
};

USTRUCT()
struct BROKENEDGE_API FExtractionOrigin
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Lot;
	UPROPERTY()
	int32 Quantity = 0;
};

USTRUCT(BlueprintType)
struct BROKENEDGE_API FExtractionItem
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Id;
	UPROPERTY()
	FName Definition;
	UPROPERTY()
	int32 Quantity = 1;
	UPROPERTY()
	FIntPoint Cell = FIntPoint::ZeroValue;
	UPROPERTY()
	FIntPoint Size = FIntPoint(1, 1);
	UPROPERTY()
	bool Rotated = false;
	/** -1 = grid, 0 = held weapon, 1 = slung weapon. */
	UPROPERTY()
	int32 Slot = -1;
	UPROPERTY()
	int32 Magazine = 0;
	/** Server provenance survives splits, merges and loading into magazines. */
	UPROPERTY(NotReplicated)
	TArray<FExtractionOrigin> Origins;
	UPROPERTY(NotReplicated)
	TArray<FExtractionOrigin> MagazineOrigins;
	FIntPoint Extent() const { return Rotated ? FIntPoint(Size.Y, Size.X) : Size; }
};

USTRUCT(BlueprintType)
struct BROKENEDGE_API FExtractionContainer
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Id;
	UPROPERTY()
	int32 Revision = 1;
	UPROPERTY()
	int32 Width = 6;
	UPROPERTY()
	int32 Height = 4;
	UPROPERTY()
	bool WeaponSlots = false;
	UPROPERTY()
	TArray<FExtractionItem> Items;
	FExtractionItem* Find(const FGuid& Item);
	const FExtractionItem* Find(const FGuid& Item) const;
	const FExtractionItem* InSlot(int32 Slot) const;
	int32 Count(FName Definition) const;
};

UENUM()
enum class EExtractionItemOp : uint8 { Transfer, Drop, Use };

USTRUCT()
struct BROKENEDGE_API FExtractionItemRequest
{
	GENERATED_BODY()
	UPROPERTY()
	uint32 Sequence = 0;
	UPROPERTY()
	uint32 Session = 0;
	UPROPERTY()
	EExtractionItemOp Op = EExtractionItemOp::Transfer;
	UPROPERTY()
	bool FromLoot = false;
	UPROPERTY()
	bool ToLoot = false;
	UPROPERTY()
	FGuid Item;
	UPROPERTY()
	FGuid MergeInto;
	UPROPERTY()
	int32 Quantity = 1;
	UPROPERTY()
	int32 InventoryRevision = 0;
	UPROPERTY()
	int32 LootRevision = 0;
	UPROPERTY()
	FIntPoint Cell = FIntPoint::ZeroValue;
	UPROPERTY()
	int32 Slot = -1;
	UPROPERTY()
	bool Rotated = false;
	UPROPERTY()
	bool AutoPlace = true;
};

/** Copy/validate/commit: a failed operation never changes either container. */
namespace ExtractionItems
{
	BROKENEDGE_API TArray<FExtractionOrigin> TakeOrigins(TArray<FExtractionOrigin>& Source, int32 Quantity);
	BROKENEDGE_API void MergeOrigins(TArray<FExtractionOrigin>& Target, const TArray<FExtractionOrigin>& Source);
	BROKENEDGE_API void SeedOrigins(FExtractionItem& Item);
	BROKENEDGE_API FExtractionItem Make(FName Definition, int32 Quantity = 1);
	BROKENEDGE_API bool Fits(const FExtractionContainer& Container, const FExtractionItem& Item);
	BROKENEDGE_API bool AutoPlace(FExtractionContainer& Container, FExtractionItem Item);
	BROKENEDGE_API bool Transfer(FExtractionContainer& Source, FExtractionContainer& Destination,
	                             const FExtractionItemRequest& Request, FString& Error);
	BROKENEDGE_API bool Validate(const FExtractionContainer& Container, FString& Error);
}
