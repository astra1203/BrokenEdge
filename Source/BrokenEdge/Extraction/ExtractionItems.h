#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ExtractionItems.generated.h"

UENUM(BlueprintType)
enum class EExtractionItemKind : uint8 { Weapon, Ammo, Medical, Valuable, Armor, Attachment };

UENUM(BlueprintType)
enum class EExtractionAttachmentSlot : uint8
{
	None, Optic, Grip, Muzzle, Receiver, Barrel, Handguard, Stock, Magazine, Rail, PistolGrip
};

UENUM()
enum class EWeaponModifierOperation : uint8 { Add, Multiply, Replace };

USTRUCT(BlueprintType)
struct FWeaponAttributeModifier
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	FName Attribute;
	UPROPERTY(EditAnywhere)
	EWeaponModifierOperation Operation = EWeaponModifierOperation::Add;
	UPROPERTY(EditAnywhere)
	float Value = 0;
	UPROPERTY(EditAnywhere)
	int32 Priority = 0;
};

USTRUCT(BlueprintType)
struct FWeaponPartTune
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	FName Id;
	UPROPERTY(EditAnywhere)
	float Minimum = 0;
	UPROPERTY(EditAnywhere)
	float Maximum = 100;
	UPROPERTY(EditAnywhere)
	float Default = 50;
	UPROPERTY(EditAnywhere)
	float Step = 1;
	UPROPERTY(EditAnywhere)
	FName Attribute;
	UPROPERTY(EditAnywhere)
	EWeaponModifierOperation Operation = EWeaponModifierOperation::Add;
	UPROPERTY(EditAnywhere)
	TObjectPtr<class UCurveFloat> Curve;
};

USTRUCT()
struct FWeaponTuneValue
{
	GENERATED_BODY()
	UPROPERTY() FName Key;
	UPROPERTY() float Value = 0;
};

UENUM(BlueprintType)
enum class EExtractionMedicalEffect : uint8 { Heal, Bandage, Splint };

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
	/** Defaults preserve existing AK assets and saved ammunition identities. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon", meta=(ClampMin="1"))
	int32 MagazineCapacity = 30;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	FName DefaultAmmo = TEXT("Ammo");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	TArray<FName> CompatibleAmmo = {TEXT("Ammo"), TEXT("AmmoAP")};
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	TArray<FName> CompatibleAttachments = {TEXT("RedDot"), TEXT("VerticalGrip"), TEXT("Compensator")};
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation")
	TSoftObjectPtr<class UTexture2D> Icon;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation")
	FText Description;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation")
	FName DisplayCategory;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation", meta=(ClampMin="0", ClampMax="5"))
	int32 DisplayRarity = 0;
	bool SupportsAmmo(FName Id) const;
	bool SupportsAttachment(FName Id) const;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UStaticMesh> WorldMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Penetration = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float DamageScale = 1;
	/** 2 = helmet, 3 = vest; weapon slots 0/1 retain their original meaning. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 ArmorSlot = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 ArmorLevel = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float MaxDurability = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EExtractionMedicalEffect MedicalEffect = EExtractionMedicalEffect::Heal;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EExtractionAttachmentSlot AttachmentSlot = EExtractionAttachmentSlot::None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ScatterScale = 1;
	/** Explicit authored compatibility only; unknown source operations must never be enabled. */
	UPROPERTY(EditAnywhere, Category="Gunsmith")
	TArray<FName> ChildAttachments;
	UPROPERTY(EditAnywhere, Category="Gunsmith") TMap<FName, EExtractionAttachmentSlot> MountSockets;
	UPROPERTY(EditAnywhere, Category="Gunsmith")
	TArray<FName> ConflictingAttachments;
	UPROPERTY(EditAnywhere, Category="Gunsmith")
	bool RequiresParent = false;
	UPROPERTY(EditAnywhere, Category="Gunsmith")
	TArray<FWeaponAttributeModifier> WeaponModifiers;
	UPROPERTY(EditAnywhere, Category="Gunsmith")
	TArray<FWeaponPartTune> Tunes;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UStaticMesh> FirstPersonMesh;
	/** Optional rail adapter; its dimensions are authored in the AK component frame. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UStaticMesh> AdapterMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform AdapterMount = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform Mount = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform FirstPersonMount = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform SupportGrip = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector OpticAimOffset = FVector::ZeroVector;
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

/** Mounted parts remain individual instances, without recursive inventory containers. */
USTRUCT(BlueprintType)
struct BROKENEDGE_API FExtractionAttachment
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Id;
	UPROPERTY()
	FName Definition;
	/** Invalid parent means weapon root, including migrated legacy saves. */
	UPROPERTY()
	FGuid Parent;
	UPROPERTY() FName Socket;
	UPROPERTY()
	TArray<FWeaponTuneValue> TuneValues;
	UPROPERTY(NotReplicated)
	TArray<FExtractionOrigin> Origins;
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
	/** -1 = grid, 0 = held weapon, 1 = slung weapon, 2 = helmet, 3 = vest. */
	UPROPERTY()
	int32 Slot = -1;
	UPROPERTY()
	int32 Magazine = 0;
	UPROPERTY()
	FName MagazineAmmo = TEXT("Ammo");
	UPROPERTY()
	float Durability = 0;
	UPROPERTY()
	TArray<FExtractionAttachment> Attachments;
	/** Loose attachments retain precision tuning when detached or traded. */
	UPROPERTY()
	TArray<FWeaponTuneValue> TuneValues;
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
enum class EExtractionItemOp : uint8 { Transfer, Drop, Use, Attach, Detach, Unload, SelectAmmo, Tune };

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
	/** Server resolves both instances; clients never supply resulting equipment state. */
	UPROPERTY()
	FGuid Weapon;
	UPROPERTY()
	EExtractionAttachmentSlot AttachmentSlot = EExtractionAttachmentSlot::None;
	UPROPERTY()
	FName AmmoDefinition;
	UPROPERTY()
	FGuid ParentAttachment;
	UPROPERTY() FName AttachmentSocket;
	UPROPERTY()
	FGuid MountedAttachment;
	UPROPERTY()
	FName TuneId;
	UPROPERTY()
	float TuneValue = 0;
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
	BROKENEDGE_API int32 MagazineCapacity(const FExtractionItem& Item);
	BROKENEDGE_API TArray<FString> WeaponDetails(const FExtractionItem& Item);
	struct FEquipmentAction
	{
		FString Label;
		FExtractionItemRequest Request;
	};

	BROKENEDGE_API TArray<FEquipmentAction> EquipmentActions(const FExtractionContainer& Container, FGuid Selected);
	BROKENEDGE_API TArray<FExtractionOrigin> TakeOrigins(TArray<FExtractionOrigin>& Source, int32 Quantity);
	BROKENEDGE_API void MergeOrigins(TArray<FExtractionOrigin>& Target, const TArray<FExtractionOrigin>& Source);
	BROKENEDGE_API void SeedOrigins(FExtractionItem& Item);
	BROKENEDGE_API FExtractionItem Make(FName Definition, int32 Quantity = 1);
	BROKENEDGE_API bool Fits(const FExtractionContainer& Container, const FExtractionItem& Item);
	BROKENEDGE_API bool AutoPlace(FExtractionContainer& Container, FExtractionItem Item);
	BROKENEDGE_API bool Transfer(FExtractionContainer& Source, FExtractionContainer& Destination,
	                             const FExtractionItemRequest& Request, FString& Error);
	BROKENEDGE_API bool Validate(const FExtractionContainer& Container, FString& Error);
	BROKENEDGE_API bool ValidateItem(const FExtractionItem& Item, bool Provenance, FString& Error);
	BROKENEDGE_API bool Configure(FExtractionContainer& Container, const FExtractionItemRequest& Request,
	                              FString& Error);
	BROKENEDGE_API float ScatterScale(const FExtractionItem& Weapon);
	BROKENEDGE_API FString Details(const FExtractionItem& Item);
	BROKENEDGE_API FExtractionItem Unmount(const FExtractionAttachment& Part);
}
