// Copyright Daniil Grigoriev. All Rights Reserved.

using UnrealBuildTool;

public class AsyncScreenShotTests : ModuleRules
{
    public AsyncScreenShotTests(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Tests for the AsyncScreenShot runtime module: save-path helper and the metadata
        // JSON writer. GPU capture paths need a real frame and are not covered here.
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "AsyncScreenShot"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Json"
        });
    }
}
