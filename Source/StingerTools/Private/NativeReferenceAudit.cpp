#include "NativeReferenceAudit.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/PackagePath.h"
#include "UObject/LinkerLoad.h"
#include "UObject/Package.h"

FString UNativeReferenceAudit::InspectPackage(const FString& Filename, const TArray<FString>& ClassPaths)
{
    const auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("read"), false);
    const FPackagePath Path = FPackagePath::FromLocalPath(Filename);
    // Avoid disrupting packages already owned by the editor or an asynchronous loader.
    FlushAsyncLoading();
    const bool AlreadyLoaded = !Path.GetPackageName().IsEmpty() && FindPackage(nullptr, *Path.GetPackageName());
    if (auto* Linker = GetPackageLinker(nullptr, Path, LOAD_NoVerify, nullptr))
    {
        Result->SetBoolField(TEXT("read"), true);
        Result->SetStringField(TEXT("package"), Path.GetPackageName());
        TArray<TSharedPtr<FJsonValue>> Hard, Soft, Names;
        auto Matches = [&](const FString& ObjectPath)
        {
            for (const auto& Target : ClassPaths)
                if (ObjectPath == Target || ObjectPath.StartsWith(Target + TEXT("."))
                    || ObjectPath.StartsWith(Target + TEXT(":"))) return true;
            return false;
        };
        for (int32 I = 0; I < Linker->ImportMap.Num(); ++I)
        {
            const FString Imported = Linker->GetImportPathName(I);
            if (Matches(Imported)) Hard.Add(MakeShared<FJsonValueString>(Imported));
        }
        for (const auto& Reference : Linker->SoftObjectPathList)
            if (Matches(Reference.ToString())) Soft.Add(MakeShared<FJsonValueString>(Reference.ToString()));
        // Older packages can store soft paths as names instead of a dedicated path table.
        for (const auto NameId : Linker->NameMap)
        {
            const FString Name = FName::CreateFromDisplayId(NameId, 0).ToString();
            if (Matches(Name)) Names.Add(MakeShared<FJsonValueString>(Name));
        }
        Result->SetArrayField(TEXT("hard_references"), Hard);
        Result->SetArrayField(TEXT("soft_references"), Soft);
        Result->SetArrayField(TEXT("path_names"), Names);
        if (!AlreadyLoaded) ResetLoaders(Linker->LinkerRoot);
    }
    FString Json;
    FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json));
    return Json;
}
