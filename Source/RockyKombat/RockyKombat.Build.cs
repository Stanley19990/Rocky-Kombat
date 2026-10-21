// RockyKombat.Build.cs
//
// Module rules for the single Rocky Kombat runtime module.
//
// Dependency notes:
//  * The combat core (Combat/RKCCombatCore.*) is deliberately pure C++ with no
//    engine headers, so it also compiles in the standalone MSVC harness in
//    Tools/CombatCoreTests. It needs no extra module dependencies.
//  * Paper2D supplies the sprite/paper-flipbook types used once the art pack is
//    imported. It is already linked so that importing sprites does not require a
//    Build.cs change mid-milestone.
//  * UMG/Slate are used by the HUD.

using UnrealBuildTool;

public class RockyKombat : ModuleRules
{
	public RockyKombat(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// LAYOUT DECISION: this module keeps a flat, folder-grouped layout
		// (AI/, Arena/, Combat/, Fighters/, Match/, UI/, Visual/) instead of the
		// Public/Private split. Includes are therefore written module-relative
		// ("Combat/RKCCombatCore.h"), which is also how the pure-C++ harnesses in
		// Tools/ reference the combat core. UBT does not add the module folder to
		// the search path by default in that layout, so it is added here.
		// Without this line every module-relative include fails with C1083
		// (observed: first RockyKombatEditor build, 2026-10-17).
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Paper2D"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UMG",
			"Slate",
			"SlateCore",
			"WebSockets",
			"Json",
			// FDisplayMetrics::TitleSafePaddingSize is the platform's safe area
			// (display cut-out and gesture bar on a phone) and lives in
			// ApplicationCore. It is a public dependency of SlateCore, but the
			// touch controller includes it directly, so it is named here rather
			// than relied on transitively.
			"ApplicationCore"
		});
	}
}
