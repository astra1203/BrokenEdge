#include "BrokenEdgeCharacterPresentation.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
    bool Matches(FName Bone, const TArray<FName>& Names, const TArray<FString>& Prefixes)
    {
        if (Names.Contains(Bone)) return true;
        const FString Name = Bone.ToString();
        return Prefixes.ContainsByPredicate([&](const FString& Prefix) { return !Prefix.IsEmpty() && Name.StartsWith(Prefix); });
    }
}
int32 UBrokenEdgeCharacterPresentation::ArmorSlotForBone(FName Bone) const
{
    return HeadBones.Contains(Bone) ? 2 : Matches(Bone, TorsoBones, TorsoPrefixes) ? 3 : INDEX_NONE;
}
bool UBrokenEdgeCharacterPresentation::IsLeg(FName Bone) const { return Matches(Bone, LegBones, LegPrefixes); }

TArray<FString> UBrokenEdgeCharacterPresentation::ValidatePresentation() const
{
    TArray<FString> Errors;
    if (!WorldMesh) Errors.Add(TEXT("WorldMesh is missing"));
    if (!WorldAnimClass) Errors.Add(TEXT("WorldAnimClass is missing"));
    if (!!FirstPersonMeshAsset != !!FirstPersonAnimClass) Errors.Add(TEXT("FirstPerson mesh and animation class must be configured together"));
    auto CheckSkeleton = [&](USkeletalMesh* Mesh, UClass* Class, const TCHAR* Label)
    {
        if (const auto* Anim = Cast<UAnimBlueprintGeneratedClass>(Class); Mesh && Anim && Anim->TargetSkeleton != Mesh->GetSkeleton())
            Errors.Add(FString(Label) + TEXT(" animation skeleton does not match mesh"));
    };
    CheckSkeleton(WorldMesh, WorldAnimClass, TEXT("World"));
    CheckSkeleton(FirstPersonMeshAsset, FirstPersonAnimClass, TEXT("FirstPerson"));
    if (WorldMesh)
    {
        for (FName Socket : {WeaponHandSocket, WeaponBackSocket})
            if (Socket.IsNone() || (!WorldMesh->FindSocket(Socket) && WorldMesh->GetRefSkeleton().FindBoneIndex(Socket) == INDEX_NONE))
                Errors.Add(TEXT("World attachment socket missing: ") + Socket.ToString());
        if (WorldMesh->GetRefSkeleton().FindBoneIndex(FirstPersonHeadBone) == INDEX_NONE)
            Errors.Add(TEXT("World head bone missing: ") + FirstPersonHeadBone.ToString());
        bool HasHead = false, HasTorso = false, HasLeg = false;
        for (const auto& Bone : WorldMesh->GetRefSkeleton().GetRefBoneInfo())
        {
            HasHead |= HeadBones.Contains(Bone.Name);
            HasTorso |= Matches(Bone.Name, TorsoBones, TorsoPrefixes);
            HasLeg |= IsLeg(Bone.Name);
        }
        if (!HasHead) Errors.Add(TEXT("Head armor partition matches no world bones"));
        if (!HasTorso) Errors.Add(TEXT("Torso armor partition matches no world bones"));
        if (!HasLeg) Errors.Add(TEXT("Leg trauma partition matches no world bones"));
    }
    if (FirstPersonMeshAsset)
        for (FName Bone : RequiredFirstPersonBones)
            if (FirstPersonMeshAsset->GetRefSkeleton().FindBoneIndex(Bone) == INDEX_NONE)
                Errors.Add(TEXT("Required first-person bone missing: ") + Bone.ToString());
    for (const auto& Anim : {StandToProneAnim, ProneIdleAnim, ProneToStandAnim, ProneFireAnim, ProneReloadAnim})
        if (!Anim.IsNull() && !Anim.LoadSynchronous()) Errors.Add(TEXT("Cannot load prone animation: ") + Anim.ToString());
    if (FirstPersonWeaponFit.bEnabled && (!FirstPersonWeaponSourceMesh || !FirstPersonWeaponVisualMesh))
        Errors.Add(TEXT("Enabled weapon fit requires source and visual meshes"));
    return Errors;
}
#if WITH_EDITOR
EDataValidationResult UBrokenEdgeCharacterPresentation::IsDataValid(FDataValidationContext& Context) const
{
    const auto Errors = ValidatePresentation();
    for (const auto& Error : Errors) Context.AddError(FText::FromString(Error));
    return Errors.IsEmpty() ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
