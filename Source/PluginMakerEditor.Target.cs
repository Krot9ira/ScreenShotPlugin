// Copyright 2023 Grigoryev Daniil. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class PluginMakerEditorTarget : TargetRules
{
	public PluginMakerEditorTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("PluginMaker");
	}
}
