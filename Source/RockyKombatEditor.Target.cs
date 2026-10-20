// RockyKombatEditor.Target.cs
//
// Editor target, used by the editor and by headless tooling
// (UnrealEditor-Cmd.exe -run=... / -ExecutePythonScript=...).

using UnrealBuildTool;
using System.Collections.Generic;

public class RockyKombatEditorTarget : TargetRules
{
	public RockyKombatEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.Add("RockyKombat");
	}
}
