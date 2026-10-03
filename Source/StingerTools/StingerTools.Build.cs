using UnrealBuildTool;

public class StingerTools : ModuleRules
{
    public StingerTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "AssetRegistry", "BlueprintGraph", "AnimGraph", "AnimGraphRuntime", "Kismet", "AnimationDataController", "EnhancedInput", "InputCore", "Json", "BrokenEdge", "UMG", "MultiplayerSessions" });
    }
}
