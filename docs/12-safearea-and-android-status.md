# 12 - Safe-area support, desktop repackage, and the Android APK blocker

Written 2026-10-19. This document records what was built and verified in this
session, and exactly what one remaining action is needed from the operator
before an Android APK can exist.

## 1. Safe-area support for the on-screen touch pad (DONE, verified)

### What the problem was

The on-screen pad placed the stick and the action buttons at fixed fractions of
the whole viewport. On a phone that has a display cut-out, rounded corners or a
gesture bar, the bottom corners of the viewport are not usable, so a control
could be drawn under the system UI, or its hit region could overlap an area the
system swallows.

### What was implemented

The pad now reads the platform's safe rectangle at run time and lays every
control out inside it. A platform that reports no inset keeps the old
whole-viewport mapping exactly, so nothing changes on a desktop.

| File | Change |
| --- | --- |
| `Source/RockyKombat/UI/RKCTouchControls.cpp` | New console variable `rkc.SafeAreaInsets` (`"L,T,R,B"` in pixels, also settable as a command-line switch `-rkc.SafeAreaInsets=40,0,40,30`); helpers `ParseInsetsPx`, `ClampAxisInsets`, `ClampSafeInsets`, and the public entry points `GetSafeRect`, `SetSafeInsetsPx`, `EffectiveSafeInsetsPx`, `ReadPlatformSafeInsetsPx`. `CentrePx`, `RadiusPx`, `ControlAt` and `GetStickKnobOffsetPx` take the inset into account. |
| `Source/RockyKombat/UI/RKCTouchControls.h` | Declares the safe-area API and documents the invariant: a missing or nonsense metric can only ever shrink the usable rectangle, never grow it. |
| `Source/RockyKombat/Match/RKCPlayerController.cpp/.h` | `RefreshSafeArea()` is called from `PlayerTick`: it calls `FDisplayMetrics::RebuildDisplayMetrics` and hands the result to the pad, throttled to 1 Hz and re-run immediately whenever the viewport size changes. It also drops a held guard when the safe rectangle actually changes shape, so a moving layout cannot leave a finger latched on a button that moved out from under it. |
| `Source/RockyKombat/UI/RKCHUD.cpp` | `DrawTouchPad` passes `Touch.GetSafeInsetsPx()` into `CentrePx`/`RadiusPx`, and the touch-pad caption line reports the insets and whether the pad is inside the safe rectangle. |
| `Source/RockyKombat/RockyKombat.Build.cs` | `ApplicationCore` added to `PrivateDependencyModuleNames` (the module that owns `FDisplayMetrics`). |
| `Source/RockyKombat/Match/RKCGameModePresentationSelfTest.cpp` | New section **E. SAFE AREA** with 8 assertions, so the behaviour is machine-checked, not just eyeballed. |

### How the inset is obtained

`RKCTouch::ReadPlatformSafeInsetsPx()` prefers the explicit `rkc.SafeAreaInsets`
override, and otherwise reads `FDisplayMetrics::TitleSafePaddingSize`
(a `FVector4`: left, top, right, bottom) after
`FDisplayMetrics::RebuildDisplayMetrics`. The value is then clamped by
`ClampSafeInsets` against the viewport the layout is using, so a bogus metric
cannot collapse the pad.

### Evidence

| Check | Result | Record |
| --- | --- | --- |
| Editor target rebuilt with the change | `Result: Succeeded`, 9 of 9 actions | `Saved/build_safearea.log` |
| Headless self-test, editor, 3 matches | **199 PASS, 0 FAIL** | `Saved/Evidence/RKC_SELFTEST_20261019_174059.txt` |
| Packaged-binary self-test, 3 matches | **198 PASS, 0 FAIL** | `Packaged/Windows/RockyKombat/Saved/Evidence/RKC_SELFTEST_20261019_180259.txt` |
| Section E safe-area assertions | 8 of 8 PASS in both runs | sections quoted below |
| Live platform inset on this machine (desktop) | none reported, so the pad covers the whole viewport - the intended no-op path | `live pad safe area: L=0 T=0 R=0 B=0 px of 1280x720` |

The eight assertions, verbatim from the packaged record:

```
--- E. SAFE AREA ---
PASS  no safe area reported -> the layout is exactly the plain viewport mapping (6 of 6 controls)
PASS  a cut-out and a gesture bar shrink the usable rectangle to (40,0)-(2360,1050)
PASS  every control is drawn wholly inside the safe rectangle (6 of 6)
PASS  hit testing follows the drawn pad: the pause control moved 257 px and is live only at its new centre (old spot: nothing)
PASS  an absurd metric is clamped, not believed: the usable rectangle keeps 896x612 of 1280x720 px (negative inset -> 0)
PASS  every control still lands inside the clamped rectangle (6 of 6)
PASS  re-reading the same safe area keeps a held guard (yes); a changed one drops it (fingers now 0)
PASS  -rkc.SafeAreaInsets reaches the reader as an override: the variable exists and 63,0,17,29 came back as 63,0,17,29
```

### What is still not verified

The *real* phone inset. No Android device was attached to this machine, so the
number the pad receives on actual hardware has not been observed. Everything
downstream of that number is covered by the assertions above.

## 2. Desktop build repackaged (DONE)

`Scripts/package_win64.bat` was re-run so the packaged game contains the
safe-area code (the previous package predated it).

- Result: **BUILD SUCCESSFUL**, `AutomationTool exiting with ExitCode=0 (Success)`
- Time: `BuildCookRun time: 400.15 s` (6 m 43 s wall clock)
- Archive: `C:\RockyKombat\Packaged\Windows\` (931 MB)
- Executable: `C:\RockyKombat\Packaged\Windows\RockyKombat.exe` (2026-10-19 18:01)
- Content: 2597 UFS files, packed into `RockyKombat-Windows.pak` (10.2 MB) plus
  the IoStore containers `RockyKombat-Windows.ucas` (125.7 MB) /
  `.utoc`
- Packaged self-test: **198 PASS, 0 FAIL**
- Log: `Saved/package_safearea.log`

## 3. Android APK: toolchain ready, engine payload missing

### The toolchain itself is correct and accepted

Running UBT's platform validation with the downloaded toolchain on the
environment (`JAVA_HOME`, `ANDROID_HOME`, `NDKROOT`) reports:

```
##PlatformValidate: Android VALID r27c
Registering build platform: Android - buildable: True
```

Installed and verified on this machine:

| Component | Version | Location |
| --- | --- | --- |
| JDK | 17.0.13+11 | `C:\AndroidSetup\jdk\jdk-17.0.13+11` |
| Android SDK platform | android-36 | `C:\Users\DELL\AppData\Local\Android\Sdk\platforms` |
| Build-tools | 36.1.0 (and 36.0.0) | `...\Android\Sdk\build-tools` |
| NDK | 27.2.12479018 (r27c) | `...\Android\Sdk\ndk` |
| CMake | 3.22.1 | `...\Android\Sdk\cmake` |
| Platform-tools | r37.0.1 | `...\Android\Sdk\platform-tools` |

### The blocker: the engine is missing its Android platform payload

This engine install is a Windows-only launcher install. The UE 5.8 build
registers Android *toolchain* support (UBT's `AndroidPlatformSDK.cs`,
`UEBuildAndroid.cs`, `UEDeployAndroid.cs`, `AndroidProjectGenerator.cs` and the
AutomationTool `AndroidPlatform.Automation.cs` are all present, as is
`Engine/Source/Runtime/Android`), but the Android **platform download** was never
installed. Attempting the Android target produces, verbatim:

```
Log file: C:\Users\DELL\AppData\Local\UnrealBuildTool\Log.txt
Missing files required to build Android targets. Enable Android as an optional download component in the Epic Games Launcher.

Result: Failed (OtherCompilationError)
```

That message comes from `UnrealBuildTool/Configuration/Descriptors/TargetDescriptor.cs`
(around line 529), which raises it whenever
`InstalledPlatformInfo.IsValid(..., EProjectType.Code, InstalledPlatformState.Downloaded)`
is false for the platform. The platform table lives in
`Engine/Config/BaseEngine.ini`, section `[InstalledPlatforms]`, and the Android
entry is gated on this required file:

```
+InstalledPlatformConfigurations=(PlatformName="Android", Configuration="Development", PlatformType="Game", Architecture="arm64", RequiredFile="Engine\Binaries\Android\UnrealGame.target", ...)
```

Confirmed absent on this machine:

| Path | Purpose | State |
| --- | --- | --- |
| `Engine\Binaries\Android\` (with `UnrealGame.target`) | prebuilt engine binaries for Android plus the "downloaded" marker | missing (`Engine\Binaries` contains only `DotNET`, `Mac`, `ThirdParty`, `Win64`) |
| `Engine\Build\Android\Java\` | the GameActivity Java sources copied into every generated APK project | missing |
| `Engine\Build\Android\Prebuilt\` | prebuilt Android Java libraries copied into the APK project | missing |
| `Engine\Platforms\Android\` | platform extension content | contains only `Plugins\Animation` |

`UEDeployAndroid.cs` line 4246 onwards resolves
`GetUnrealBuildFilePath(EngineDirectory)` = `<Engine>\Build\Android\Java` and
`GetUnrealPreBuiltFilePath(EngineDirectory)` = `<Engine>\Build\Android\Prebuilt`
and copies from them when it generates the Gradle project, so even if the
platform gate were bypassed, no APK could be produced without those files. There
is no legitimate way to synthesise them: they are the engine's own Android Java
runtime and its prebuilt Android binaries.

### The one action required

In Epic Games Launcher: **Unreal Engine -> Library -> the "..." / options chevron
next to UE 5.8 -> Options -> tick "Android" under target platforms -> Apply.**
That downloads the Android platform payload (roughly 1.5-3 GB) into this same
install. Nothing else about the machine needs to change - the JDK, SDK, NDK and
build-tools are already in place and already validate.

### What is ready to run the moment that download finishes

| File | Purpose |
| --- | --- |
| `Config/DefaultEngine.ini` (`[/Script/AndroidRuntimeSettings.AndroidRuntimeSettings]`) | Package name `com.rockycat.rockykombat`, display name "Rocky Kombat", version 1.0.0 / store version 1, min SDK 26, target SDK 36, landscape, full screen, content packed inside the APK (single self-contained file, no `.obb`), ARM64 only, ES3.1 + Vulkan, no Google Play services. |
| `Config/Android/AndroidEngine.ini` | Android-only renderer overrides: no HDR, no virtual shadow maps, no motion blur/bloom/AO/auto-exposure, mobile anti-aliasing, deterministic physics. |
| `Build/Android/rkc.keystore` | A debug signing key (alias `rkcdebug`, passwords `rockykombat`) so successive APKs share an identity and install over each other instead of being refused. Generated with `keytool`, 2048-bit RSA, 10000 days. This is a debug key and must not be used for a store release. |
| `Scripts/validate_android.bat` | Exports `JAVA_HOME`/`ANDROID_HOME`/`NDKROOT` and runs UBT's platform validation. Expect `##PlatformValidate: Android VALID r27c`. |
| `Scripts/build_android.bat` | Compiles the Android ARM64 game target through the NDK. |
| `Scripts/package_android.bat` | Full `BuildCookRun` for Android: build, cook (`-cookflavor=Multi`), stage, pak/iostore, package, archive to `Saved\Android`. Takes `Development` (default) or `Shipping`. |

### Verification status of the Android path

- Verified: the toolchain is accepted by UBT (`Android VALID r27c`); the packaging
  scripts, config, keystore and safe-area code are all in place on disk.
- **Not** verified: whether the APK actually builds, installs and renders. That
  cannot be tested until the engine payload above is present, and any claim that
  the Android build works would be unfounded until then.


## 4. Android blocker, re-validated 2026-10-19 19:29 — one missing engine file

The Android SDK/NDK/JDK situation is finished business. `Scripts\validate_android.bat`, which
exports `JAVA_HOME=C:\AndroidSetup\jdk\jdk-17.0.13+11`,
`ANDROID_HOME=C:\Users\DELL\AppData\Local\Android\Sdk` and
`NDKROOT=%ANDROID_HOME%\ndk\27.2.12479018`, now prints:

```
##PlatformValidate: Win64 VALID 10.0.22621.0
##PlatformValidate: Mac INVALID 26.1.1
##PlatformValidate: IOS INVALID 26.1.1
##PlatformValidate: Android VALID r27c
##PlatformValidate: Linux INVALID v26_clang-20.1.8-rockylinux8
```

`Android VALID r27c` is the line that matters: UBT accepts the toolchain.

What is still missing is on the engine side, and it is a single marker file plus the payload behind
it:

- `Engine\Config\BaseEngine.ini` lines 4008–4013 declare each Android game configuration with
  `RequiredFile="Engine\Binaries\Android\UnrealGame.target"`.
- UBT's `InstalledPlatformInfo` reports state `Downloaded` only if that file exists
  (`Engine\Source\Programs\UnrealBuildTool\System\InstalledPlatformInfo.cs:362`).
- `TargetDescriptor.cs` (~line 519) converts that into:
  `Missing files required to build Android targets. Enable Android as an optional download component in the Epic Games Launcher.`
- On this machine `Engine\Binaries\Android` and `Engine\Build\Android` do not exist at all;
  the only Android content the engine shipped is `Engine\Platforms\Android\Plugins\Animation`.

Fresh evidence: `Saved\Evidence\android_validate.log`, `Saved\Evidence\validate_android_stdout.txt`.

### The one human action that unblocks the APK

The component is a launcher download (roughly 4.5 GB) and there is no command-line path for it —
Epic exposes optional engine components only through the launcher UI, and this engine's artefact
has `bAllowUriCmdArgs: false` in its `.item` manifest, so a URI/deep-link cannot be used either.

1. Open the Epic Games Launcher (it was already running on this machine at the time of writing).
2. **Unreal Engine → Library** tab.
3. Find the **UE 5.8** tile and click the **⋯ / dropdown** next to **Launch** → **Options**.
4. Under **Installation options**, scroll to **Target Platforms** and tick **Android**.
5. Click **Apply** and let the ~4.5 GB download finish.
6. Then, from this project:
   ```bat
   Scripts\validate_android.bat        REM expect Android VALID r27c still
   Scripts\build_android.bat            REM ARM64 game target through the NDK
   Scripts\package_android.bat          REM BuildCookRun: build, cook, stage, package, archive to Saved\Android
   ```
7. Expected first failure if anything is still wrong: `Missing files required to build Android
   targets` (engine component missing), a JDK/SDK version error (environment variables not exported —
   use the script, not a bare `RunUAT` call), or a Gradle error (the generated Gradle project needs
   `ANDROID_HOME` and a writable intermediate directory).

Nothing else on the Android path is outstanding on the game's side: package name, landscape,
safe-area handling, ES3.1/Vulkan mobile render path, single self-contained APK content and a debug
keystore that keeps successive installs updatable over each other are all in the repository and
covered by the self-test.


---

## Re-checked 2026-10-19 20:20 — still one launcher click away

The blocker is unchanged and was re-tested rather than assumed:

```
test -d "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Android"   ->  DIR ABSENT
```

There is still no Android engine payload on this machine, so `Engine\Config\BaseEngine.ini`
lines 4008–4013 keep UBT from targeting Android. The toolchain is fine: the same session
re-ran the build and the editor reported `PlatformValidate: Android VALID r27c` whenever
`JAVA_HOME`, `ANDROID_HOME` and `NDKROOT` were exported (`Scripts\validate_android.bat` does
that; a bare shell without those variables reports `INVALID r27c`, which is an environment
statement, not a regression).

Nothing on the game side changed or needs changing: landscape lock, safe-area insets, the
mobile render path the touch pad draws into, the offline asset set and the touch controls are
all implemented and asserted by the packaged self-test. When the launcher adds the ~4.5 GB
Android component under **Library → UE 5.8 → ⋯ → Options → Target Platforms → Android**, the
remaining steps are `Scripts\package_android.bat` and then a device install; the APK has never
been built, so phone playability is untested rather than disproved.
