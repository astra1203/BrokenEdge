#include "GeneratedWidgetGuard.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "UObject/UnrealType.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    FString Sidecar(UWidgetBlueprint* BP)
    {
        const FString Package = BP->GetOutermost()->GetName();
        if (!Package.StartsWith(TEXT("/Game/")) || Package.Contains(TEXT(".."))) return {};
        return FPaths::ProjectConfigDir() / TEXT("GeneratedWidgets") / (Package.Mid(6) + TEXT(".json"));
    }
    void Properties(UObject* Object, const FString& Prefix, GeneratedWidgetGuard::FSnapshot& Out)
    {
        for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
        {
            if (!It->HasAnyPropertyFlags(CPF_Edit) || It->HasAnyPropertyFlags(CPF_Transient)) continue;
            FString Value;
            It->ExportText_InContainer(0, Value, Object, nullptr, Object, PPF_None);
            Out.Add(Prefix + TEXT("/") + It->GetName(), Value);
        }
    }
}

GeneratedWidgetGuard::FSnapshot GeneratedWidgetGuard::Snapshot(UWidgetBlueprint* BP)
{
    FSnapshot Result;
    Result.Add(TEXT("ParentClass"), GetPathNameSafe(BP->ParentClass));
    Result.Add(TEXT("Root"), GetNameSafe(BP->WidgetTree->RootWidget));
    TArray<UWidget*> Widgets;
    BP->WidgetTree->GetAllWidgets(Widgets);
    for (auto* Widget : Widgets)
    {
        const FString Key = TEXT("Widget/") + Widget->GetName();
        Result.Add(Key + TEXT("/Class"), Widget->GetClass()->GetPathName());
        const auto* Parent = Widget->GetParent();
        Result.Add(Key + TEXT("/Parent"), GetNameSafe(Parent));
        Result.Add(Key + TEXT("/Order"), FString::FromInt(Parent ? Parent->GetChildIndex(Widget) : 0));
        Properties(Widget, Key, Result);
        if (Widget->Slot) Properties(Widget->Slot, Key + TEXT("/Slot"), Result);
        // Child UserWidget internals belong to their own assets, never this tree.
    }
    return Result;
}

FString GeneratedWidgetGuard::Difference(const FSnapshot& Previous, const FSnapshot& Current)
{
    TArray<FString> Changes;
    for (const auto& Entry : Previous)
    {
        const FString* Value = Current.Find(Entry.Key);
        if (!Value) Changes.Add(TEXT("removed ") + Entry.Key);
        else if (*Value != Entry.Value)
            Changes.Add(TEXT("changed ") + Entry.Key + TEXT(": ") + Entry.Value.Left(160) + TEXT(" -> ") + Value->Left(160));
    }
    for (const auto& Entry : Current)
        if (!Previous.Contains(Entry.Key)) Changes.Add(TEXT("added ") + Entry.Key);
    Changes.Sort();
    return FString::Join(Changes, TEXT("\n"));
}

FString GeneratedWidgetGuard::Preflight(const TArray<FString>& PackagePaths)
{
    const bool Force = FParse::Param(FCommandLine::Get(), TEXT("ForceGeneratedWidgets"));
    TArray<FString> Errors;
    for (const auto& Package : PackagePaths)
    {
        if (!FPackageName::DoesPackageExist(Package)) continue;
        auto* BP = LoadObject<UWidgetBlueprint>(nullptr, *Package);
        if (!BP || !BP->WidgetTree) return TEXT("FAILED: invalid widget blueprint ") + Package;
        FString Text;
        TSharedPtr<FJsonObject> Json;
        double Schema = 0;
        FString RecordedPackage;
        const TSharedPtr<FJsonObject>* Values = nullptr;
        if (!FFileHelper::LoadFileToString(Text, *Sidecar(BP)) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json ||
            !Json->TryGetNumberField(TEXT("schema_version"), Schema) || Schema != 1 ||
            !Json->TryGetStringField(TEXT("package"), RecordedPackage) || RecordedPackage != Package ||
            !Json->TryGetObjectField(TEXT("snapshot"), Values))
        {
            Errors.Add(Package + TEXT(": missing/invalid generation baseline"));
            continue;
        }
        FSnapshot Previous;
        bool Valid = true;
        for (const auto& Entry : (*Values)->Values)
        {
            FString Value;
            if (!Entry.Value->TryGetString(Value)) { Valid = false; break; }
            Previous.Add(FString(Entry.Key.ToView()), MoveTemp(Value));
        }
        const FString Changes = Valid ? Difference(Previous, Snapshot(BP)) : TEXT("invalid snapshot value");
        if (!Changes.IsEmpty()) Errors.Add(Package + TEXT(":\n") + Changes);
    }
    if (Errors.IsEmpty()) return {};
    const FString Message = FString::Join(Errors, TEXT("\n"));
    if (Force)
    {
        UE_LOG(LogTemp, Warning, TEXT("Explicit ForceGeneratedWidgets override:\n%s"), *Message);
        return {};
    }
    return TEXT("FAILED: generated widget overwrite refused. Review differences; --force explicitly replaces them.\n") + Message;
}

bool GeneratedWidgetGuard::Record(UWidgetBlueprint* BP)
{
    const FString Path = Sidecar(BP);
    if (Path.IsEmpty()) return false;
    auto Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("schema_version"), 1);
    Json->SetStringField(TEXT("package"), BP->GetOutermost()->GetName());
    auto Values = MakeShared<FJsonObject>();
    auto State = Snapshot(BP);
    TArray<FString> Keys;
    State.GetKeys(Keys); Keys.Sort();
    for (const auto& Key : Keys) Values->SetStringField(Key, State[Key]);
    Json->SetObjectField(TEXT("snapshot"), Values);
    FString Text;
    if (!FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text))) return false;
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true)) return false;
    const FString Temporary = Path + TEXT(".tmp");
    return FFileHelper::SaveStringToFile(Text, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        && IFileManager::Get().Move(*Path, *Temporary, true, true);
}
