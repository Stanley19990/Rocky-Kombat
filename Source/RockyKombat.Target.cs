// RockyKombat.Target.cs
//
// Standalone game target. Produces RockyKombat.exe for a packaged Win64 build
// (acceptance gate A: "project compiles, runs, and packages").

using UnrealBuildTool;
using System.Collections.Generic;

public class RockyKombatTarget : TargetRules
{
	public RockyKombatTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.Add("RockyKombat");
	}
}
