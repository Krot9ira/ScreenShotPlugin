// Copyright 2023 Grigoryev Daniil. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class PluginMakerTarget : TargetRules
{
	public PluginMakerTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("PluginMaker");
	}
}
