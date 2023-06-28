// Copyright 2023 Grigoryev Daniil. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class PluginMakerTarget : TargetRules
{
	public PluginMakerTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V2;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_1;
		ExtraModuleNames.Add("PluginMaker");
	}
}
