using UnrealBuildTool;

public class StingerTools : ModuleRules
{
    public StingerTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "UMGEditor", "Slate", "SlateCore", "RenderCore" });
        PrivateDependencyModuleNames.Add("StaticMeshDescription");
        PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "AssetRegistry", "BlueprintGraph", "AnimGraph", "AnimGraphRuntime", "Kismet", "AnimationDataController", "EnhancedInput", "InputCore", "Json", "BrokenEdge", "UMG", "MultiplayerSessions", "MeshDescription", "ImageWrapper", "NavigationSystem" });
    }
}
