# 04 — Evidence

Everything here was produced by running the commands in 02 and reading the logs. Numbers are copied from the logs, not summarised from memory. Anything not yet observed is listed as **not verified** rather than implied.

## A. Combat rules, no engine involved

```
cmd.exe /c build_suite.bat > suite_run.log 2>&1        (Tools\CombatCoreTests)
```

```
=================================================================
 364 checks run, 0 failed
 RESULT: PASS
=================================================================
```

22 test sections (T1..T18 plus calibration), covering round/knockdown rules, damage and stamina, hit detection and the counter-hit window, blocking and dodging, arena bounds and the anti-overlap separation rule, the bounded event queue, and deterministic replay.

## B. Offline AI, no engine involved, warnings-as-errors

```
cmd.exe /c build_ai_tests.bat > ai_tests.log 2>&1      (Tools\AITests)
```

```
================ SUMMARY ================
checks run : 224
failures   : 0
result     : PASS
========================================

==== EXIT CODE 0 ====
```

The same log contains the interesting behavioural numbers rather than just pass marks:

```
=== 7. Difficulty changes discipline, not outcomes by fiat ===
      rookie defensive orders                          10.000
      champion defensive orders                        27.000
      rookie decisions                                184.000
      champion decisions                              286.000

=== 8. Asymmetric fighters ===
      mirror match ticks                             1535.000
      mixed match ticks                               612.000
      mixed match damage A                             84.888
      mixed match damage B                             24.350
```

Champion-grade opponents choose to defend roughly 2.7× as often as Rookie-grade ones while the damage model is untouched — the numbers move because the AI is more disciplined, not because it hits harder.

Sections covered: profile integrity and reflex monotonicity; per-tick invariants (positions, separation, health/stamina ranges, knockdown limits); agreement with `RKC::RunBotMatch` computed by hand in the harness; determinism across repeated seeds; training instrumentation being additive (hitboxes and frame data never change an outcome); configuration neutrality; bot activity; difficulty behaviour; and input-path equivalence.

## C. Editor module build

```
cmd.exe /c Scripts\build_editor.bat > build_editor.log 2>&1
```

```
[1/6] Compile [x64] Module.RockyKombat.gen.cpp
[2/6] Compile [x64] RKCGameMode.cpp
[3/6] Compile [x64] RKCHUD.cpp
[4/6] Link [x64] UnrealEditor-RockyKombat.dll
...
Result: Succeeded
Total execution time: 159.18 seconds
```

Clean-build reference earlier in the session: 12 translation units, 206.69 s, producing `Binaries\Win64\UnrealEditor-RockyKombat.dll`.

## D. In-engine self-test, 2026-10-17 14:14

```
UnrealEditor-Cmd.exe C:\RockyKombat\RockyKombat.uproject -game -nullrhi -log -unattended \
    -nosplash -nopause -stdout -RKCSelfTest=3 > selftest_headless.log 2>&1
```

Process exit code **0**. Record: `Saved\Evidence\RKC_SELFTEST_20261017_141456.txt`.

```
ROCKY KOMBAT - IN-ENGINE SELF-TEST
===================================

engine:       5.8.3-58210709+++UE5+Release-5.8
headless:     yes (-nullrhi, HUD draw counts not asserted)
matches:      3
rules:        3 round(s) of 30 s, countdown 3 s, min separation 58 cm

--- WORLD ---
PASS  the match world was built
PASS  a ring actor exists
PASS  the ring has geometry (26 pieces)
PASS  both fighter actors exist
PASS  both fighters have a rig (8 / 8 parts)
PASS  a match camera exists
PASS  the match HUD class is ARKCHUD
PASS  the live simulation carries a valid rule set

--- MATCHES (real director loop, fixed 1/60 s steps) ---
match 1: seeds 20261017/911043  ticks 1321 (22.0 s)  result 2  end 1  score 9-10  hits 8-11  checksum 0x817E602C
PASS  match 1 raised a result banner ("SHADOW WINS BY KNOCKOUT   9 - 10")
PASS  match 1 filled the HUD event feed (6 lines)
match 2: seeds 20261019/911045  ticks 1565 (26.1 s)  result 1  end 1  score 10-9  hits 7-7  checksum 0xEC4A3020
PASS  match 2 raised a result banner ("ROCKY WINS BY KNOCKOUT   10 - 9")
match 3: seeds 20261021/911047  ticks 701 (11.7 s)  result 2  end 1  score 9-10  hits 2-9  checksum 0x28F2ACC7
PASS  no match timed out
PASS  no match was left undecided
PASS  no tick breached minimum separation (worst 0.000000 cm)
total: 3587 tick(s) simulated, 44 landed punch(es)

--- ENGINE vs PURE-C++ CROSS-CHECK ---
reference run (pure C++ harness, same seeds):
result=B wins end=KO scores=9-10 rounds=1 ticks=701 time=11.7s | A: hits=2 thrown=8 kd=0 dmg=27 hp=0 | B: hits=9 thrown=9 kd=0 dmg=102 hp=65 | blocks=0/0 dodges=0/1 decisions=57/58 orders=8/9 sep=78.6 worstViolation=0.000
agreement with the live engine match: YES
PASS  the live engine match and the pure-C++ harness agree on the same seeds

--- DETERMINISM ---
repeat run: ticks 701, checksum 0x28F2ACC7 vs 0x28F2ACC7
PASS  repeating the same seeds reproduces the same final state
distinct match checksums: yes
PASS  different seeds produce different matches

--- HUD ---
drawn frames 0, layout regions 0, canvas 0x0
headless run (-nullrhi): there is no canvas, so the draw counts above are reported but not asserted.

--- RESULT ---
failed checks: 0
result:        PASS
```

What this establishes: the **engine build** (not just the standalone one) builds a complete world, plays complete matches to a decided knockout result, keeps the separation rule exactly, produces the HUD's banner text and event-feed content from live state, agrees with the pure-C++ harness on the same seeds, and reproduces bit-identical final state when re-run. The three match checksums are all different, so seeds genuinely drive distinct matches.

## D2. Packaged build: cook, archive, and the packaged self-test, 2026-10-17 19:35–19:38

```
cmd.exe /c Scripts\package_win64.bat > packaged.log 2>&1
```

```
********** BUILD COMMAND COMPLETED **********
********** COOK COMMAND COMPLETED **********
********** STAGE COMMAND COMPLETED **********
********** ARCHIVE COMMAND COMPLETED **********
BuildCookRun time: 240.72 s
BUILD SUCCESSFUL
AutomationTool exiting with ExitCode=0 (Success)
```

`LogInit: Display: Success - 0 error(s), 0 warning(s)`. The archive is `C:\RockyKombat\Packaged\Windows\RockyKombat.exe` (171 520 bytes; `Packaged` totals 903 MB), and `Packaged\Windows\Manifest_UFSFiles_Win64.txt` lists 149 entries under `RockyKombat/Art`, which is how the imported artwork is shown to survive the cook. (The first attempt failed with `ExitCode=25` on an outdated VC++ redistributable; that is written up as K7 in 06.)

The archive was then run through the *same* self-test, from the packaged binary rather than the editor:

```
Packaged\Windows\RockyKombat.exe -nullrhi -log -unattended -stdout -RKCSelfTest=2 > packaged_selftest.log 2>&1
```

Process exit code **0**. Record: `Packaged\Windows\RockyKombat\Saved\Evidence\RKC_SELFTEST_20261017_193618.txt`.

```
--- IMPORTED ARTWORK (present) ---
PASS  the arena presents the imported backdrop image
PASS  fighter A presents imported flipbooks
PASS  fighter A resolved all 8 animations (got 8)
PASS  fighter B presents imported flipbooks
PASS  fighter B resolved all 8 animations (got 8)

--- MATCHES (real director loop, fixed 1/60 s steps) ---
  fighters: sim A x=-149.2 B x=-78.8 (sep 70.5 cm) | actor A x=-149.2 B x=-78.8
PASS  match 1 draws each fighter at the simulation's own X
match 1: seeds 20261017/911043  ticks 1321 (22.0 s)  result 2  end 1  score 9-10  hits 8-11  checksum 0x817E602C
match 2: seeds 20261019/911045  ticks 1565 (26.1 s)  result 1  end 1  score 10-9  hits 7-7  checksum 0xEC4A3020
PASS  no tick breached minimum separation (worst 0.000000 cm)
total: 2886 tick(s) simulated, 33 landed punch(es)

--- ENGINE vs PURE-C++ CROSS-CHECK ---
agreement with the live engine match: YES
PASS  the live engine match and the pure-C++ harness agree on the same seeds

--- RESULT ---
failed checks: 0
result:        PASS
```

Two things this adds over the editor run in D. The first is that the **packaged** binary resolves the imported artwork at runtime through its string paths and reports the arena backdrop, both fighters' flipbooks and all eight animations per fighter as present — the same thing the pak manifest shows from the outside. The second is that the shipped build reproduces the editor build's match outcomes exactly: the same seeds give the same checksums (`0x817E602C`, `0xEC4A3020`) and the same tick counts (1321, 1565), so packaging and cooking changed nothing about the simulation.

### D2.1 Windowed rendering from the packaged build

The packaged binary was also launched windowed and two frames were captured with `Tools\capture_window.ps1`:

```
Packaged\Windows\RockyKombat.exe -windowed -ResX=1280 -ResY=720
```

| Frame | Path | Shows |
| --- | --- | --- |
| Start of round | `Saved\Evidence\packaged_shot_early.png` | Both fighters apart at their start positions, `ROUND 1 OF 3`, countdown `0.6`, health/stamina bars, `ROUND WON 0` on both sides, the event feed and the control-hint footer |
| Mid-round | `Saved\Evidence\packaged_shot_clinch.png` | Fighters closed to a clinch, HUD fully populated, imported arena backdrop behind them, no engine warning text |

The early frame is the evidence for the requested start-distance check: the two fighters begin on opposite sides of the ring. The simulation's own start positions are `ArenaMinX+120 = -300` and `ArenaMaxX-120 = +300` cm with `ArenaMinX=-420`, `ArenaMaxX=+420`, i.e. 600 cm apart, and the HUD reports the separation in cm next to the fighters.

The `packaged_shot_clinch.png` frame was captured **after** the forward-shading-priority change and shows no `ForwardShadingPriority` warning, whereas the earlier editor frame `Saved\Evidence\window_shot.png` did. See K8 in 06 for the caveat that this does not fully isolate the fix.

Both packaged runs also wrote their own engine log to `Packaged\Windows\RockyKombat\Saved\Logs\`, and both contain the input setup line:

```
LogRockyKombat: Player input bound (scheme=hot-seat).
```

(`RockyKombat-backup-2026.10.17-18.36.18.log` for the headless self-test run, `RockyKombat.log` for the windowed run.) The absence of the guard message `Player input component is missing; the match has no human controls.` in either log means the input component existed and every action/axis binding in `SetupInputComponent()` was registered at runtime — in the **packaged** build, not just the editor. This does not prove a key press reaches the simulation; that is still a human playtest (E.3), but it rules out the "the shipped build never set up input" failure.

### D3 Windowed self-test: the HUD asserted, not skipped

`Scripts\run_selftest_windowed.bat 3` runs the same self-test with a real renderer, which is the only
way the HUD assertions are made at all (`-nullrhi` reports the draw counts and deliberately does not
assert them):

```
UnrealEditor-Cmd.exe RockyKombat.uproject -game -windowed -ResX=1280 -ResY=720 -nosplash -unattended -stdout -RKCSelfTest=3
```

Result: `Saved\Evidence\RKC_SELFTEST_20261018_000537.txt` — **167 checks, 0 failed**, `result: PASS`,
and the HUD section reads:

```
drawn frames 5, layout regions 6, canvas 1280x720
PASS  the HUD completed at least one draw pass
PASS  the HUD emitted the full layout (6 regions)
PASS  the HUD drew into a real canvas
```

An earlier windowed run on the same build line (`Saved\Evidence\RKC_SELFTEST_20261017_234141.txt`,
also 167 checks and 0 failures) reports the same counts, so the numbers are stable across runs.

One trap worth knowing when reading these runs: `FPlatformMisc::RequestExitWithStatus` is ignored by
the generic platform implementation on Windows, so the process can return a non-zero shell status even
for a PASS. The verdict is the record's `result:` line, not `%ERRORLEVEL%`.

### D4 Every presentation stage photographed by the game itself, 2026-10-17 23:58–00:02

`Scripts\capture_stages.bat` runs the real game windowed and lets **the game** take the pictures: the
director asks the renderer for one screenshot on the frame each named screen is up, and writes a
manifest recording what the HUD had drawn at that moment (`rkc.CaptureStages`, CVar-gated, off
default). A window grab from outside cannot tie an image to a named screen or to a draw count, and can
just as easily photograph a stale or black frame.

```
Scripts\capture_stages.bat 3 C:\RockyKombat\capture_stages.log
  -> UnrealEditor-Cmd.exe RockyKombat.uproject -game -windowed -ResX=1280 -ResY=720 -nosplash
     -unattended -stdout -rkc.CaptureStages=1 -rkc.FrontEndAuto=3 -rkc.TouchForced=1 -rkc.QuickRules=1
```

Output folder: `Saved\Evidence\Stages\20261017_235858\` — seven PNGs plus `manifest.txt` and
`pixel_verification.json`.

| Stage | Screen | Age when asked | HUD at that moment (from the manifest) |
| --- | --- | --- | --- |
| `01_startup.png` | Boot | 0.25 s | drew — 2 draw passes since the previous stage, 3 layout regions, canvas 1280x720 |
| `02_loading.png` | Loading | 0.25 s | drew — 128 passes, 10 regions, 1280x720 |
| `03_menu.png` | Menu | 0.50 s | drew — 101 passes, 15 regions, 1280x720 |
| `04_versus.png` | Versus | 0.50 s | drew — 100 passes, 11 regions, 1280x720 |
| `05_match.png` | Match | 1.28 s | drew — 15 passes, 12 regions, 1280x720 |
| `06_touchpad.png` | Match | 3.61 s | drew — 13 passes, 13 regions, 1280x720 |
| `07_results.png` | Results | 0.50 s | drew — 60 passes, 14 regions, 1280x720 |

The manifest's own verdict is `photographed 7 of 7 stage(s); HUD drawing reported on 7; missed 0`, and
the flow was the game's real one: Boot → Menu → match 1 (quick rules, ended and recorded in
`Saved\Evidence\RKC_20261018_000100_match-end.txt`) → Menu → Versus → match 2 (ended 00:02:20,
`RKC_20261018_000220_match-end.txt`) → Results. Both match records report `1 round(s) of 20 s`, which
is the `rkc.QuickRules=1` switch actually reaching the rules.

`Tools\Diagnostics\verify_stage_shots.py` then reads the pixels back and checks what a blank capture
cannot pass — the requested size, real colour variety, the near-black share, and the HUD's own light
text and accent pixels:

```
PASS  01_startup.png   1280x720  colours  102  near-black  0.0%  text px  2506  accent px  2160
PASS  02_loading.png   1280x720  colours  125  near-black  0.0%  text px  1907  accent px  3208
PASS  03_menu.png      1280x720  colours  218  near-black  0.0%  text px  2237  accent px  2199
PASS  04_versus.png    1280x720  colours  157  near-black  0.0%  text px  2044  accent px  2166
PASS  05_match.png     1280x720  colours 2087  near-black 26.2%  text px 80091  accent px 31689
PASS  06_touchpad.png  1280x720  colours 2084  near-black 25.1%  text px 79746  accent px 31841
PASS  07_results.png   1280x720  colours  217  near-black  0.0%  text px  2080  accent px  2260
7 of 7 screenshot(s) verified; overall PASS
```

The frames were also looked at directly. The cards carry their headings and rows (the main menu, the
versus card, and the result card with its rematch / change-fighters / main-menu / exit rows); the match
frames show the ring, both fighters, the health and stamina bars, the round counter and the event feed;
and the touch-pad discs (`JAB`, `BLOCK`, `DODGE`) are on screen in the touch-pad frame.

**One deviation to know about:** the run passes `rkc.TouchForced=1` so that the on-screen pad can be
photographed on a desktop build, and the pad is therefore drawn over *every* frame in this set, not only
`06_touchpad.png`. In normal play the pad is hidden on the menus. The manifest records the setting for
exactly this reason.

## E. What is NOT verified

1. **An independent HUD pixel assertion — now largely closed.** D4 adds an automated pixel check
   (`Tools\Diagnostics\verify_stage_shots.py`) over seven stage screenshots, each tied to a named screen
   and to a HUD draw count taken in the same frame. It proves the frames are not blank, that they carry
   a full-size canvas, and that the HUD's text and accent colours are on them. It does not prove the
   picture is the *right* screen — that is the manifest's screen name and draw counts — and it is not a
   comparison against a stored reference image. See K6 in 06 for the remaining limitation.

2. **Packaged build — now verified.** Superseded by D2: `Packaged\Windows\RockyKombat.exe` exists, runs, and passes its own self-test with matching checksums, so gate A's packaging half is satisfied. Retained here so the earlier uncertainty is not silently dropped.

3. **Human input path in a running game.** The binding code exists (`RKC_MoveX`, `RKC_MoveX2`, `RKC_P1Jab`, `RKC_P1Heavy`, `RKC_P1Block`, `RKC_P1Dodge`, the P2 equivalents, rematch, frame-data toggle, quit) and is reachable through `Config\DefaultInput.ini`. The packaged build logs `Player input bound (scheme=hot-seat).` with no missing-component error (D2.1), so the bindings are registered at runtime in the shipped build. What remains untested is the last hop — a person actually pressing the keys and the simulation reacting. The AI harness proves that bot input travels the same `FInputState` channel as recorded human input, so the wiring past the latch is covered; the human keystroke itself is not.

4. **Gate C review and everything above it.** Art is now imported and renders (D2, D2.1), but there has been no side-by-side review against reference images (none were supplied separately). Gate D and up remain untouched: no device tested, nothing networked, nothing persistent.


## F. Distribution archive verification, 2026-10-19 19:31–19:36

The player-facing pack (`Dist\RockyKombat-Win64-1.0.0`, and the zip beside it) was verified by
unpacking the shipped archive rather than by trusting the copy step:

| Step | Command / artefact | Result |
| --- | --- | --- |
| Archive built | PowerShell `Compress-Archive -CompressionLevel Optimal` | `Dist\RockyKombat-Win64-1.0.0.zip`, 435,898,643 bytes, sha256 `670da412a342dea2b24fe28cb7c2d8aec8a51fa31d3b18a8368e897b2b2984d8` |
| Archive extracted | `[IO.Compression.ZipFile]::ExtractToDirectory` into `Dist\_verify_extract` | exit OK, 18.5 s, 940 MB unpacked |
| Every file re-hashed inside the extract | `sha256sum -c SHA256SUMS.txt` | **164 OK, 0 FAILED** |
| Extracted game run | `_verify_extract\...\RockyKombat.exe -nullrhi -unattended -stdout -RKCSelfTest=3` | `RKC SELF-TEST PASS: 0 failed check(s)`, record **200 PASS / 0 FAIL** |
| Record preserved | `Packaged\Windows\RockyKombat\Saved\Evidence\RKC_SELFTEST_20261019_193615_extracted-from-zip.txt` | kept before the temporary extract was deleted |

Key per-file hashes (full list in the pack's `SHA256SUMS.txt`):

- launcher stub `RockyKombat.exe` — `870b6f64b9076a1f03839a61ce302b32fe942fe1a6afc6e6bf497f35dc4f039d`
- game binary `RockyKombat\Binaries\Win64\RockyKombat.exe` — `e55d7580cecea7f9a0640a6a74e1ea2376ccb55b57c47147690d50b7fcc924c2`
- `RockyKombat-Windows.pak` — `ff7af048a5d79199ac338fd5b1662770ee0ad93fcfd82de2202bcaba2945ff62`

What this adds: the pack that would actually be handed to a player has been proven to extract
completely, to match its own manifest of hashes, and to run and pass the in-engine self-test from
the extracted copy. It does not add anything about how the game *feels* — that still needs a person
(04.E, `docs\08-playtest-checklist.md`).

**This entry is history.** The pack has been rebuilt three times since: 20:30 (`445,228,907` bytes,
`91b9e6f7…`), 21:50 (`452,873,834` bytes, `819674a9…`) and 21:59:46 (`452,883,760` bytes, sha256
`d4c67c861ff29890141a1a7fb9ae9ab16d108bba6acdf8ebe9ac417e0da420ca`, 181 files, `180 OK / 0 FAILED`,
guide checker 9 of 9, extracted self-test 219 checks / 0 failed at 22:00:40). The current figures and
the full manifest live in `docs\13-distribution-pack.md`. The numbers above are kept because the
200-check self-test record they produced still travels inside the pack.

## G. Android platform validation, 2026-10-19 19:29

`Scripts\validate_android.bat` (which exports `JAVA_HOME`, `ANDROID_HOME` and `NDKROOT` first) was
run again to pin the blocker down to one line. UBT's own platform validation reports:

```
##PlatformValidate: Win64 VALID 10.0.22621.0
##PlatformValidate: Android VALID r27c
##PlatformValidate: IOS INVALID 26.1.1
```

So the Android *toolchain* is accepted (`r27c` = the installed NDK 27.2.12479018). The refusal comes
from the engine distribution's own installed-platform list: `Engine\Config\BaseEngine.ini` lines
4008–4013 declare every Android game configuration with
`RequiredFile="Engine\Binaries\Android\UnrealGame.target"`, UBT's `InstalledPlatformInfo` treats a
missing `RequiredFile` as "platform not downloaded" (`System\InstalledPlatformInfo.cs:362`), and
`TargetDescriptor.cs:519` turns that into the build error
`Missing files required to build Android targets. Enable Android as an optional download component
in the Epic Games Launcher.`

Evidence: `Saved\Evidence\android_validate.log` and `Saved\Evidence\validate_android_stdout.txt`.
The directory `Engine\Binaries\Android` does not exist on this machine, and neither does
`Engine\Build\Android`; `Engine\Platforms\Android\Plugins\Animation` is the only Android content
that shipped. That is the whole reason no APK exists — not the SDK, not the code, not the signing key.

## H. Black-frame sweep across launch: three bursts, 2026-10-19 20:38 and 2026-10-20 13:04/13:05

*Establishes: no black frame is presented after the game has drawn, and the leading black frames
are the window's empty surface, not a black screen.*

Each burst was started by `Tools\Diagnostics\rkc_blackwatch_capture.ps1`, which launches the
packaged build itself and begins grabbing from process launch (not from a window handle appearing),
so the first seconds of the process are covered. Frames were measured by
`rkc_blackwatch_analyze.py` and located in time by `rkc_black_sequence.py`.

| Capture | Frames | Cadence | Black (<8) | First lit | After first lit |
|---|---|---|---|---|---|
| `20261019_203836` | 126 | ~630 ms | 7 | 8 | 0 |
| `20261020_130441` | 45 | ~440 ms | 21 | 22 | 0 |
| `20261020_130553` | 55 | ~327 ms | 31 | 32 | 0 |

Analyzer output for the tightest run (`black_sequence_verdict.txt`):

```
FRAMES 55  BLACK(<8.0) 31
FIRST_LIT_FRAME 32
BLACK_FRAME_INDICES [1..31]
ALL_BLACK_FRAMES_PRECEDE_FIRST_LIT True
BLACK_FRAMES_AFTER_PICTURE_WAS_UP 0 []
FIRST_LIT_FILE frame_00032.png
  frame_mean 36.99  frame_black_frac 0.000
  backdrop 42.38  left_fighter 38.72  right_fighter 39.14
VERDICT: PASS - every black frame precedes the first presented frame; no black screen was ever
displayed after the game had drawn
```

Supporting measurements, from the same captures:

- **One image, repeated.** All leading black frames in all three runs share SHA-1
  `29f74898ee322526` — a single frozen surface, identical across three separate launches.
- **No dark frame later.** Applying a stricter cut of mean 20 to everything after the first lit
  frame leaves 0 frames in every run; the lowest means observed are 29.26, 36.94 and 36.94.
- **The first lit frame is the boot card.** `frame_00008.png` (Oct 19) shows "ROCKYCAT ECOSYSTEM
  presents" over the night arena with both fighters visible, and the first lit frame of all three
  runs measures identically (`36.99 / 42.38 / 38.72 / 39.14`), so the presentation is deterministic.
- **Structurally corroborated.** The machine's first screen is `EScreen::Boot`
  (`Source\RockyKombat\UI\RKCFrontEnd.cpp:1422` returns the heading `ROCKYCAT ECOSYSTEM`), so UI
  exists from the first presented frame; the HUD contains no fade-from-black overlay.

**What this does not establish.** `PrintWindow` costs 327–630 ms per grab on this host, so a black
flash shorter than roughly 0.3 s between two samples would not be seen. The sweep excludes a
sustained black screen, a black screen behind the loading card and a fade through black; it does
not exclude a single-rendered-frame flicker. Nothing in the build records per-frame luminance, so
that last case remains unmeasured rather than disproved.

Artifacts: `Saved\Evidence\blackwatch\20261019_203836\`, `...\20261020_130441\`,
`...\20261020_130553\` — each with `analysis.json`, `black_sequence_verdict.txt` and the frames.

## I. The floor nameplate: diagnosed as code, removed, and measured before/after, 2026-10-20

**What was wrong.** Every match frame carried a black mass across both fighters' legs — the
`ROCKY` / `MIDDLEWEIGHT` text that `ARKCFighter::NameText` drew from the fighter's root, at
ground level, in black instead of the gold and blue the code requested.

**Proof the text was not in the artwork.** Three separate looks at the supplied PNG
(`ArtSource\RockyKombatPack_v1.0\Arena\Midnight_Rooftop_Ring.png`, 1672x941):

| Check | Artifact | Result |
|---|---|---|
| crop at the left nameplate position | `Saved\Evidence\wordmark_crop_L.png` | plain painted floor |
| crop at the right nameplate position | `Saved\Evidence\wordmark_crop_R.png` | plain painted floor |
| full-width floor band, high-passed | `Saved\Evidence\arena_floor_band.png` | no lettering; 11.21% strong-edge share is plank seams and paint texture |

The earlier "tiled wordmark" reading came from an autocorrelation scan whose dominant period
(48 px) was the ring railing and rope lines. That measurement described the room, not a wordmark.

**The fix.** The component is gone from `ARKCFighter` (declaration, construction, per-roster
colour, part count and per-frame rotation). The HUD already names both fighters in the side
panels and in the centre scoreboard, so identity never depended on it.

**Before / after, same region, same probe.** `Tools\Diagnostics\rkc_nameplate_probe.py` on
(260,430)-(350,470) of the match frame:

| Metric | `Stages\20261019_184243\05_match.png` (pre-fix) | `Stages\20261020_133731\05_match.png` (post-fix) |
|---|---|---|
| near-black pixels (lum < 12) | 1794 / 3600 = 49.83% | 623 / 3600 = 17.31% |
| mean of darkest tenth | rgb 0.0, 0.0, 0.0 | rgb 4.8, 5.3, 40.0 |

The remaining dark share is the painted floor's own plank and shadow detail; the fighters' legs
are unobstructed in the new frame.

**Rebuild chain, end to end.**

| Step | Command | Result |
|---|---|---|
| editor build | `Scripts\build_editor.bat` | `Result: Succeeded`, 6 actions, 140 s (`build_nameplatefix.log`) |
| headless self-test, 3 matches | `Scripts\run_selftest_headless.bat 3` | PASS, 0 failed checks (`Saved\Evidence\RKC_SELFTEST_20261020_133009.txt`) |
| package | `Scripts\package_win64.bat` | `BUILD SUCCESSFUL`, BuildCookRun 344.71 s (`package_nameplatefix.log`) |
| packaged self-test | `Scripts\run_selftest_packaged.bat 3` | PASS, 0 failed checks (`Saved\Evidence\RKC_SELFTEST_20261020_133708.txt`) |
| windowed stage capture, shipped binary | `RockyKombat.exe -windowed -rkc.CaptureStages=1 -rkc.FrontEndAuto=3 -rkc.QuickRules=1` | 6 of 7 stages photographed with the HUD drawing; `06_touchpad` expected-missed without `-rkc.TouchForced=1` |

Artifacts: `Saved\Evidence\Stages\20261020_133731\` (frames, `manifest.txt`),
`Saved\Evidence\PKG_SELFTEST_nameplatefix.log`, `Saved\Evidence\PKG_CAPTURE_nameplatefix.log`.
