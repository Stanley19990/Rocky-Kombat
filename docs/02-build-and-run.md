# 02 — Build and run

All commands are run from `C:\RockyKombat`. Every script writes a log next to itself or into the project root; the logs are the evidence, not the console scrollback.

## 1. Compile the editor module

```bat
cmd.exe /c Scripts\build_editor.bat > build_editor.log 2>&1
```

- `Result: Succeeded` and `Output binary: C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe` is the success condition.
- Last verified run: 6 build actions (2 translation units recompiled + link), 159.18 s total.
- Clean-build reference: 12 translation units, 206.69 s, producing `Binaries\Win64\UnrealEditor-RockyKombat.dll`.
- If UnrealHeaderTool output looks stale after adding or renaming a `UCLASS`, delete `Intermediate\Build` and rebuild.

## 2. Combat-core rule harness (no engine at all)

```bat
cd Tools\CombatCoreTests
cmd.exe /c build_suite.bat > suite_run.log 2>&1
```

Compiles `Source\RockyKombat\Combat\RKCCombatCore.cpp` with plain MSVC `/W4` and runs the rule checks. Last verified run: **364 checks run, 0 failed, RESULT: PASS**.

## 3. Offline AI harness (no engine at all)

```bat
cd Tools\AITests
cmd.exe /c build_ai_tests.bat > ai_tests.log 2>&1
```

Compiles the shipped `RKCCombatCore.cpp` **and** the shipped `AI\RKCAI.cpp` with `/W4 /WX` (warnings are build errors here, which is how the AI is kept engine-clean), links them with no Unreal libraries, and runs the AI suite. Last verified run: **224 checks, 0 failures, EXIT CODE 0**.

`RKCRoster.cpp` is deliberately excluded from this harness because it includes `CoreMinimal.h`; the harness supplies its own contender balance table instead, so the AI stays testable without the engine.

## 4. In-engine self-test (the engine build, verified from inside the engine)

Headless, no GPU needed:

```bat
cd "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64"
UnrealEditor-Cmd.exe C:\RockyKombat\RockyKombat.uproject -game -nullrhi -log -unattended ^
    -nosplash -nopause -stdout -RKCSelfTest=3 > C:\RockyKombat\selftest_headless.log 2>&1
```

With rendering, which additionally asserts that the HUD really draws:

```bat
UnrealEditor-Cmd.exe C:\RockyKombat\RockyKombat.uproject -game -windowed -ResX=1280 -ResY=720 ^
    -log -nosplash -stdout -RKCSelfTest=2 > C:\RockyKombat\selftest_windowed.log 2>&1
```

- `-RKCSelfTest=N` runs `N` complete bot-versus-bot matches (default 3, clamped to 1..50) through the real per-frame director loop, then writes its record and exits the process.
- Records land in `Saved\Evidence\RKC_SELFTEST_<timestamp>.txt`, and the record's `result:` line is the verdict — **not `%ERRORLEVEL%`**, because `FPlatformMisc::RequestExitWithStatus` is ignored by the generic platform implementation on Windows, so a PASS can still leave a non-zero shell status.
- On a `-nullrhi` run the HUD draw counters are reported but not asserted, because no canvas exists; the record says so explicitly. The windowed form above is what asserts them.

The windowed run is also scripted, which is how it is reproduced:

```bat
cmd.exe /c Scripts\run_selftest_windowed.bat 3 C:\RockyKombat\selftest_windowed5.log
```

### 4.1 Stage capture: a screenshot of every presentation screen, with the HUD's own numbers

```bat
cmd.exe /c Scripts\capture_stages.bat 3 C:\RockyKombat\capture_stages.log
REM  -> UnrealEditor-Cmd.exe RockyKombat.uproject -game -windowed -ResX=1280 -ResY=720 -nosplash
REM     -unattended -stdout -rkc.CaptureStages=1 -rkc.FrontEndAuto=3 -rkc.TouchForced=1 -rkc.QuickRules=1
```

The game takes the pictures itself (`rkc.CaptureStages`), one per stage of the plan in
`RKCGameMode.cpp` (startup, loading, menu, versus, match, touch pad, results), and writes
`Saved\Evidence\Stages\<timestamp>\manifest.txt` beside them recording the screen, the screen age and
what the HUD had drawn at that moment. It also leaves when the set is complete (use
`rkc.CaptureStages=2` to photograph and keep the window). An unseen stage is recorded as `MISSED`
rather than quietly dropped.

Check the set afterwards without eyes:

```bat
python Tools\Diagnostics\verify_stage_shots.py Saved\Evidence\Stages\<timestamp> --json Saved\Evidence\Stages\<timestamp>\pixel_verification.json
```

It decodes each PNG and asserts the requested size, colour variety, a bounded near-black share and
the presence of the HUD's own text and accent pixels — the properties a black or blank capture cannot
have. Last verified run: **7 of 7 stages photographed, 7 with the HUD drawing, 0 missed; 7 of 7 pixel
checks PASS** (04.D4).

## 5. Package, cook, stage and archive the Win64 game (gate A)

```bat
cmd.exe /c Scripts\package_win64.bat > packaged.log 2>&1
```

Archives to `C:\RockyKombat\Packaged`. The shipped executable is
`Packaged\Windows\RockyKombat.exe`.

- Last verified run: **BUILD SUCCESSFUL, ExitCode 0, 0 errors, 0 warnings** (2026-10-17). Total staged size ~903 MB; `RockyKombat.exe` is 171,520 bytes.
- Cook requires the Visual C++ 2015-2022 x64 redistributable 14.50.35719.0 or newer; an older redist fails the cook with ExitCode 25 before any archive is written.
- `Manifest_UFSFiles_Win64.txt` in the archive root lists the packed content — the `RockyKombat/Art` sprites and flipbooks are among the 149 entries under that folder, which is what proves the artwork made it into the pak.

## 6. Normal play

There is no `.umap` to choose: `Config\DefaultEngine.ini` points the default map at `/Engine/Maps/Entry`, and `ARKCGameMode::StartPlay()` builds the ring, both fighters, the lighting and the camera at runtime and starts the match immediately. That is why the identical launch works in the editor, in the packaged build, and under `-RKCSelfTest`.

### 6.1 Packaged Development build (the runnable deliverable)

From the project root:

```bat
Packaged\Windows\RockyKombat.exe -windowed -ResX=1280 -ResY=720
```

- Executable: `C:\RockyKombat\Packaged\Windows\RockyKombat.exe` (Development configuration; last verified 171,520 bytes, 2026-10-17 19:35).
- Content ships inside the pak; the loose `Content\RockyKombat\Art` tree is not needed at run time.
- `-windowed -ResX=1280 -ResY=720` forces a windowed 1280x720 viewport, which is what makes the capture screenshots reproducible. Drop those switches to run full-screen at the desktop resolution.
- The same command line accepts `-RKCSelfTest=N`: append it to make the packaged binary run the automated self-test and exit. Unlike the editor runs, a packaged run does not need `-nullrhi` when a GPU is present.

### 6.2 In-editor

Open `RockyKombat.uproject` and press Play. The editor loads the compiled `UnrealEditor-RockyKombat.dll`; rebuild it first (section 1) after any C++ change.

### 6.3 Controls (keyboard)

`Config\DefaultInput.ini` binds both fighters on one keyboard. Player 1's keys sit on the left of the layout and player 2's on the right, so a single tester can drive both sides. Attacks are edge-triggered: a tap registers even if press and release fall inside one frame, and holding a key will not re-fire it.

| Action | Player 1 (ROCKY, side A) | Player 2 (SHADOW, side B) |
| --- | --- | --- |
| Move left / right | `A` / `D` | `Left` / `Right` |
| Block (hold) | `S` | `Down` |
| Jab (light) | `J` | `NumPad 1` |
| Heavy | `K` | `NumPad 2` |
| Dodge | `L` | `NumPad 3` |

Shared keys:

| Action | Key |
| --- | --- |
| Rematch | `R` |
| Toggle frame-data overlay | `F1` |
| Quit match | `Escape` |

### 6.4 Default match: human versus bot

`rkc.BotSide` defaults to `1`, so out of the box the bot drives side B (SHADOW) and the human plays side A (ROCKY). On a bot-driven side the bot's input takes precedence over the human latch, so pressing player 2's keys while a bot owns that side has no effect; the latch is still drained, so no stale press is replayed later. Use `rkc.BotSide=0` for two humans on one keyboard, `2` to hand side A to the bot and play as SHADOW, or `3` for a bot-versus-bot exhibition.

### 6.5 Human playtest status

Automated self-test and visible-rendering checks have passed (see `04-evidence.md`). A human-input playtest — a person actually pressing these keys through a full round — has **not** been performed yet; it is tracked as remaining work item 3 in `06-known-issues-and-remaining-work.md`.

## Console interface

Match setup is reachable without a rebuild, which is what makes scripted runs possible:

| Console variable | Effect |
| --- | --- |
| `rkc.BotSide` | 0 none, 1 bot drives B (default), 2 bot drives A, 3 both |
| `rkc.BotDifficulty` | 0 Rookie, 1 Contender (default), 2 Champion — reflexes and discipline only |
| `rkc.BotArchetypeA` / `rkc.BotArchetypeB` | 0 Balanced, 1 Pressure, 2 Counter, 3 Slugger |
| `rkc.BotSeedA` / `rkc.BotSeedB` | Bot RNG seeds; identical seeds reproduce an identical match |
| `rkc.RosterA` / `rkc.RosterB` | Fighter selection |
| `rkc.MinSeparation` | Anti-overlap floor between the fighters, in cm. `0` = rule default (58 cm); a positive value is clamped to 10..600. Raising it pushes the fighters apart, lowering it lets them stand closer before the separation correction fires |
| `rkc.QuickRules` | 1 = one 20 s round instead of three 30 s rounds |
| `rkc.TrainingA` / `rkc.TrainingB` | Training toggles (hitboxes, infinite health/stamina, no opponent input, frame data) |
| `rkc.FrameData` | Frame-data overlay |
| `rkc.Evidence` | Write a match record to `Saved\Evidence` on match end |
| `rkc.CameraFOV` / `rkc.CameraDistance` / `rkc.CameraHeight` / `rkc.CameraPitch` | Match camera |

Two more variables exist for unattended evidence runs and are off by default: `rkc.FrontEnd` (0 = skip
the front end entirely), `rkc.FrontEndAuto` (1 = answer the menus from a script; a value above 1 is
also the seconds spent on each screen), `rkc.TouchForced` (1 = draw the on-screen pad even when the
platform is not touch), `rkc.CaptureStages` (1 = photograph every presentation stage and leave when the
set is complete, 2 = photograph and stay).

**How the command-line form works.** UE 5.8 has no `FConsoleManager::SetFromCommandLine` and no
equivalent pass, so `-rkc.<Name>=<Value>` is applied by this project's own game module: it walks the
command line in `StartupModule` and sets each variable through `IConsoleManager` at
`ECVF_SetByCommandline` priority, logging every switch it applies and warning about one that names no
console variable (see K9 in 06 — before that fix the switches were silently inert). Example:

```bat
Scripts\capture_stages.bat 3 C:\RockyKombat\capture_stages.log
REM  -> -game -windowed -ResX=1280 -ResY=720 -rkc.CaptureStages=1 -rkc.FrontEndAuto=3 -rkc.TouchForced=1 -rkc.QuickRules=1
```

Console commands: `rkc.Rematch`, `rkc.Reconfigure`, `rkc.DumpState`, `rkc.BotMatch [count] [seedBase]`.

## Evidence locations

| Path | Contents |
| --- | --- |
| `build_editor.log` | Editor module build |
| `Tools\CombatCoreTests\suite_run.log`, `suite_report.txt` | 364 rule checks |
| `Tools\AITests\ai_tests.log`, `ai_tests_report.txt` | 224 AI checks |
| `selftest_headless.log` | Engine log of the headless self-test |
| `Saved\Evidence\RKC_SELFTEST_*.txt` | Self-test verdict and per-match numbers |
| `Saved\Evidence\RKC_<date>-<reason>.txt` | Per-match evidence record |
| `packaged.log` | Cook/stage/package/archive run |
| `packaged_selftest.log` | Self-test driven from the packaged binary |
| `Saved\Evidence\RKC_SELFTEST_*.txt` | Written by both the editor and the packaged build; the two records' checksums match |
| `Saved\Evidence\packaged_shot_early.png`, `packaged_shot_clinch.png` | Windowed frames captured from the packaged build (start positions, clinch, HUD) |
| `Saved\Evidence\Stages\<timestamp>\*.png`, `manifest.txt`, `pixel_verification.json` | One windowed run's evidence set: a screenshot per presentation stage, taken by the game itself, with the screen name and the HUD's draw counts recorded beside each one |
| `capture_stages.log`, `selftest_windowed*.log` | Logs of the windowed capture run and the windowed self-test runs |
| `Tools\Diagnostics\verify_stage_shots.py` | Independent pixel check of a stage folder: size, colour variety, near-black share, HUD text and accent pixels |
