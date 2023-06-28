// Copyright 2023 Grigoryev Daniil. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class PluginMakerEditorTarget : TargetRules
{
	public PluginMakerEditorTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V2;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_1;
		ExtraModuleNames.Add("PluginMaker");
	}
}
