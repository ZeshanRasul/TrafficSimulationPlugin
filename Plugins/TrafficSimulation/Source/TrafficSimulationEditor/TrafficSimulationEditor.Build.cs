using UnrealBuildTool;

public class TrafficSimulationEditor : ModuleRules
{
    public TrafficSimulationEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new[]
            {
                "Core",
                "TrafficSimulation"
            });

        PrivateDependencyModuleNames.AddRange(
            new[]
            {
                "CoreUObject",
                "Engine",
                "UnrealEd"
            });
    }
}