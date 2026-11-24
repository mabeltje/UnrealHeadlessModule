using UnrealBuildTool;

public class BlendingAutomationEditor : ModuleRules
{
    public BlendingAutomationEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "LevelSequence",
            "MovieScene",
            "MovieSceneTracks",
            "MovieSceneTools",
            "UnrealEd",

            "SequencerAbstraction",
            "UnrealVTTParser",
            "AssetFileManagement",

            "ControlRig",
            "ControlRigDeveloper",
            "ControlRigEditor"
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "BlendingAutomation" // Reference your base module if needed
        });
    }
}