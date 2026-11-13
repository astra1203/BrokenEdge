#include "ExtractionItemRegistry.h"
#include "ExtractionItems.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const UExtractionItemRegistry* UExtractionItemRegistry::Get()
{
    // The authored registry is a process-lifetime snapshot, including in PIE.
    static const TStrongObjectPtr<UExtractionItemRegistry> Registry(LoadObject<UExtractionItemRegistry>(nullptr,
        TEXT("/Game/Extraction/Registry/DA_ItemRegistry.DA_ItemRegistry")));
    return Registry.Get();
}

const UExtractionItemDefinition* UExtractionItemRegistry::Resolve(FName Id) const
{
    const auto* Reference = Entries.Find(Id);
    if (!Reference) return nullptr;
    const auto* Definition = Reference->LoadSynchronous();
    // A wrong mapping must never change the meaning of a saved inventory ID.
    return Definition && Definition->ItemId == Id ? Definition : nullptr;
}

TArray<FString> UExtractionItemRegistry::ValidateEntries() const
{
    TArray<FString> Errors;
    if (Entries.IsEmpty()) Errors.Add(TEXT("Item registry is empty"));
    for (const auto& Entry : Entries)
    {
        const FString Prefix = Entry.Key.ToString() + TEXT(": ");
        if (Entry.Key.IsNone()) Errors.Add(Prefix + TEXT("empty stable ID"));
        const auto* Definition = Entry.Value.LoadSynchronous();
        if (!Definition) Errors.Add(Prefix + TEXT("cannot load ") + Entry.Value.ToString());
        else if (Definition->ItemId != Entry.Key)
            Errors.Add(Prefix + TEXT("definition ItemId differs: ") + Definition->ItemId.ToString());
    }
    Errors.Sort();
    return Errors;
}
#if WITH_EDITOR
EDataValidationResult UExtractionItemRegistry::IsDataValid(FDataValidationContext& Context) const
{
    const auto Errors = ValidateEntries();
    for (const auto& Error : Errors) Context.AddError(FText::FromString(Error));
    return Errors.IsEmpty() ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
