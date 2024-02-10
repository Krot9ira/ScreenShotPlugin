// Copyright 2023 Grigoryev Daniil. All Rights Reserved.

using UnrealBuildTool;

public class AsyncScreenShot : ModuleRules
{
	public AsyncScreenShot(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDefinitions.Add("_WIN32_WINNT_WIN10_TH2");
        PublicDefinitions.Add("_WIN32_WINNT_WIN10_RS1");
        PublicDefinitions.Add("_WIN32_WINNT_WIN10_RS2");
        PublicDefinitions.Add("_WIN32_WINNT_WIN10_RS3");
        PublicDefinitions.Add("_WIN32_WINNT_WIN10_RS4");
        PublicDefinitions.Add("_WIN32_WINNT_WIN10_RS5");



        PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",


				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"RenderCore",
                "RHI",
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
