// Copyright (c) 2026 Daniil Grigoryev. All Rights Reserved.

using UnrealBuildTool;

public class AsyncScreenShotTests : ModuleRules
{
	public AsyncScreenShotTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Covers the parts of AsyncScreenShot that do not need a GPU: the save path, the metadata sidecar,
		// unique file naming and the crop/downscale maths. The capture paths themselves need a real frame.
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

		// The interesting pure logic - unique naming, crop and downscale - lives in the runtime module's
		// private headers. Tests are the one thing allowed to reach in there.
		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "..", "AsyncScreenShot", "Private"));
	}
}
