using UnrealBuildTool;

public class EngineWorkflowBridge : ModuleRules
{
    public EngineWorkflowBridge(ReadOnlyTargetRules Target)
        : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });

        PrivateDependencyModuleNames.AddRange(
            new[]
            {
                "AssetTools",
                "EditorFramework",
                "Projects",
                "HTTPServer",
                "Json",
                "JsonUtilities",
                "Sockets",
                "Slate",
                "SlateCore",
                "UnrealEd"
            }
        );
    }
}
