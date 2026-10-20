# 06 — Known issues and remaining work

## Known issues

### K1 — A rendering run stalls during graphics initialisation on this host (open)

Two attempts to run the game with rendering (`-game -windowed -ResX=1280 -ResY=720`) each wrote
their CSV metadata block to stdout and then nothing further for more than ten minutes, while
`Saved\Logs\RockyKombat.log` was not updated at all, i.e. the process never reached engine
initialisation. `tasklist` showed the process alive at ~250 MB, and no crash report was produced.
Both attempts were terminated by us.

- **Cause: not established.** Candidates are D3D12 device/PSO initialisation on the integrated
  UHD 620, or a graphics driver stall. We have not diagnosed it, so this is a symptom report, not
  a diagnosis.
- **Impact:** at the time, HUD pixel output was unverified and the "run it and look at it" half of
  gate B had not been done on this machine. **Superseded 2026-10-17:** windowed runs now render (the
  packaged build in 04.D2.1, the editor build in 04.D3 and 04.D4, whose stage screenshots are also
  verified at the pixel level). The stall has not reproduced since and no cause was ever established,
  so treat it as dormant rather than diagnosed — and expect the first windowed run after a code change
  to spend several minutes compiling shader permutations on this host (each job 30–50 s, logged as
  `LogShaderCompilers: ... compile time exceeded threshold`).
- **Workaround / next attempt:** run with `-nullrhi` (evidence already captured), or try
  `-d3d11 -sm5`, or run the same command on a machine with a discrete GPU. Do not conclude that
  the game itself is broken from this: the same binary, same content and same self-test path pass
  headless in ~35 s of wall time.

### K2 — Builds are serialised by memory pressure (accepted)

UnrealBuildTool reports `Requested 1.5 GB memory per action, 2.04 GB available: limiting max
parallel actions to 1` on this 8 GB machine, and the editor build took 159–207 s. Nothing is wrong
with the project; it is the host. Use `-NoUBA`/`-NoXGE` judiciously and avoid running builds
concurrently with anything else heavy.

### K3 — Editor start-up costs ~60–75 s (accepted)

Asset-registry discovery alone measured 35.8 s on this project. Scripted runs must allow for it;
do not treat a slow first log line as a hang (compare with K1, where the log does not advance at
all past RHI initialisation).

### K4 — No map asset (by design)

`Config\DefaultEngine.ini` sets `GameDefaultMap=/Engine/Maps/Entry` and `ARKCGameMode::StartPlay()`
builds the ring, fighters, lighting and camera in code. Consequences: there is no `.umap` to open,
the content browser is empty, and PIE works because the game mode default is set in
`Config\DefaultEngine.ini`. A real level asset can be introduced later without touching the rules.

### K7 — Packaging needed a newer Visual C++ redistributable (resolved)

The first `Scripts\package_win64.bat` run compiled the shipping `RockyKombat.exe` successfully
(`Binaries\Win64\RockyKombat.exe`, 331 956 736 bytes, built 14:37) but the **cook then failed**:

```
LogWindows: Error: Visual C++ redistributable version 14.44.35211.0 is outdated.
Please install version 14.50.35719.0 (or above) from Engine\Extras\Redist\en-us\vc_redist.x64.exe.
...
Took 370.69s to run UnrealEditor-Cmd.exe, ExitCode=1
Cook failed.
AutomationTool exiting with ExitCode=25 (Error_UnknownCookFailure)
```

Diagnosis: `HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64\Version` reads
`v14.44.35211.00`, while UE 5.8 requires 14.50.35719.0 or above. The engine ships exactly that
version at `Engine\Extras\Redist\en-us\vc_redist.x64.exe` (file version 14.50.35719.0), so no
download is needed.

Note the same message appears in the otherwise passing headless self-test run, where it is only a
logged engine error and the run still succeeded. It becomes fatal in cook mode, because a
commandlet treats logged engine errors as failure.

**Resolution (verified 2026-10-17 19:35).** The bundled redistributable was installed, and
`HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64\Version` now reads `v14.50.35719.00`,
which satisfies the requirement. Re-running the pipeline produced:

```
********** BUILD COMMAND COMPLETED **********
********** COOK COMMAND COMPLETED **********
********** STAGE COMMAND COMPLETED **********
********** ARCHIVE COMMAND COMPLETED **********
BuildCookRun time: 240.72 s
BUILD SUCCESSFUL
AutomationTool exiting with ExitCode=0 (Success)
```

with `LogInit: Display: Success - 0 error(s), 0 warning(s)`. The archive is
`C:\RockyKombat\Packaged\Windows\RockyKombat.exe` (171 520 bytes; `Packaged` totals 903 MB), and
`Packaged\Windows\Manifest_UFSFiles_Win64.txt` lists 149 entries under `RockyKombat/Art`, so the
imported artwork survives the cook. That matters because the art is reached only through runtime
string paths (`Source\RockyKombat\Visual\RKCArt.h`), which reference-walking cannot discover;
`+DirectoriesToAlwaysCook=(Path="/Game/RockyKombat/Art")` in `Config\DefaultGame.ini` is what keeps
it in the package.

### K5 — Missing inputs from the requester (partially resolved)

- The product brief PDF ("RockyHub Combat Game System.pdf") was never received, so brief sections
  2–13 are unimplemented and unclaimed.
- The artwork pack **was** received and is in use: `C:\Users\DELL\Downloads\Rocky_Kombat_Asset_Pack.zip`,
extracted at `C:\Users\DELL\Downloads\Rocky_Kombat_Asset_Pack\`, imported scripted into
  `Content\RockyKombat\Art` (64 textures, 64 sprites, 17 flipbooks, 3 materials, 1 arena texture; 0
errors). The earlier "no reference images / greybox only" statement is superseded for the sprite and
  arena art. There is still no separate reference-image set to review the import against.
- Re-checked 2026-10-17 19:40: `find` over `Downloads`, `Desktop`, `Documents` and the project tree
  for `*rocky*combat*.pdf`, `*combat*game*system*.pdf` and `*rocky*.pdf` returned no paths. The file
  cannot be opened because it is not on this machine; "not found" is the only error that can be
  reported, so requirements that depend on it stay `BLOCKED`.

### K6 — HUD draw counts are self-reported (largely addressed 2026-10-17)

The self-test reads the HUD's own counters. That catches "the HUD never draws", but it is not an
independent check of what the pixels contain. Two pieces have now been added (04.D3, 04.D4):

- A windowed self-test run, where the counters are asserted rather than reported (`drawn frames 5,
  layout regions 6, canvas 1280x720`).
- `Tools\Diagnostics\verify_stage_shots.py`, which decodes the seven stage screenshots and asserts the
  properties a blank capture cannot have: the requested 1280x720 size, colour variety, a bounded
  near-black share, and the HUD's own light text and accent pixels.

What is still self-reported is *which screen* each frame is: that comes from the director's screen age
and the HUD's region count, both taken in the same frame as the request. There is also no comparison
against a stored reference image, so a layout that is complete but visually wrong would pass. The two
checks together are much stronger than the counter alone, and neither replaces a person looking at the
game (04.E.3).

### K9 — The documented command-line switches were inert (fixed 2026-10-17)

Every `rkc.*` console variable was documented as settable with `-rkc.<Name>=<Value>` on the command
line. It was not: UE 5.8 has no `FConsoleManager::SetFromCommandLine` and no equivalent pass anywhere in
`Engine\Source`, so a switch that nobody reads is silently ignored. The first capture run proved it —
the log showed the front end parked on the main menu waiting for input, with `rkc.FrontEndAuto=1` and
`rkc.CaptureStages=1` both on the command line and neither having any effect (and all 41 historical
match records on this machine read `3 round(s) of 30 s`, i.e. `rkc.QuickRules=1` had never once been
applied).

**Fix:** `Source\RockyKombat\RockyKombatModule.cpp` now walks the command line at `StartupModule` and
applies every `-rkc.<Name>=<Value>` switch through `IConsoleManager` at `ECVF_SetByCommandline`
priority, logging each one and warning about a switch that names no console variable. The capture run
logs all four expected overrides before the first frame:

```
command line override: rkc.CaptureStages = 1
command line override: rkc.FrontEndAuto = 3
command line override: rkc.TouchForced = 1
command line override: rkc.QuickRules = 1
command line: 4 console override(s) applied before the first frame
```

Cross-checked in the same run: both match records report `1 round(s) of 20 s`, so the switch reached
the rules, not just the variable.

### K8 — Directional-light forward-shading warning (fixed, effect not fully isolated)

A windowed frame captured before the fix (`Saved\Evidence\window_shot.png`) shows the on-screen engine
message *"Multiple directional lights are competing to be the single one used for forward shading,
translucent, water or volumetric fog. Please adjust their ForwardShadingPriority"*. The three match
lights (key, fill, ambient) were being created with the same default forward-shading priority, so the
renderer had to pick one arbitrarily.

**Fix:** `Source\RockyKombat\Match\RKCGameMode.cpp` now carries a `ForwardPriority` field per light
spec, assigns distinct values (key 2, fill 1, ambient 0) and calls
`Comp->SetForwardShadingPriority(Spec.ForwardPriority)` on each light component.

**Evidence:** the packaged Development frame captured afterwards
(`Saved\Evidence\packaged_shot_clinch.png`) shows no warning text, while the earlier editor frame did.
Note the limitation: the later frame comes from a non-editor packaged build, so this does not isolate
how much of the silence is the explicit priorities and how much is the warning being a
`WITH_EDITOR`-only on-screen message. Both changes point the same way, but the two effects were not
separated by a test. The warning never appeared in `-nullrhi` runs, which is why the headless
self-test could not have caught it.

### K10 — Four source changes are in no built binary, and command execution is currently refused (RESOLVED 2026-10-19 — see the status update at the end of this document)

`Source\RockyKombat` holds five changes that **no build has compiled and no run has exercised**:

1. `Arena\RKCArena.cpp` / `Arena\RKCArena.h` — when the supplied Midnight Rooftop Ring image is
   active, the duplicate procedural ring (posts, turnbuckles, ropes, apron, canvas, placeholder
   slabs) is hidden by a visibility flip (`EnableArtBackdrop()` calls `SetGreyboxVisible(false)`).
   Both the `rkc.ArenaGreybox` switch and the greybox fallback are now gated on `WITH_EDITOR`
   instead of `!UE_BUILD_SHIPPING`, and `SetGreyboxVisible(true)` is refused and logged in a
   packaged build, so the duplicate ring is unreachable at runtime in Development as well as
   Shipping — a stale `Engine.ini` entry can no longer bring it back. Nothing is deleted and no
   rule, bound or collision is touched: the procedural pieces never carried collision, and the
   fight-plane bounds and anti-overlap separation live in `RKC::FCombatSim`, which this actor only
   reads.
2. `Match\RKCGameMode.cpp` — the self-test now counts ring actors in the world, and asserts at
   every stage (including after each restart, i.e. each rematch, and at the end of the run) that
   0 greybox pieces are visible, that the piece count has not grown, that the picture still carries
   the image's own aspect ratio, that the feet sit on the painted floor line, and that the picture
   covers the frame on every side. See `docs\09-arena-single-ring.md` section 4.
3. `RockyKombatModule.cpp` — a compiler-derived build stamp (`__DATE__`/`__TIME__`, engine
   version, run start time), logged and written beside `Saved\BuildStamp.txt`.
4. `docs\08-playtest-checklist.md` and `docs\09-arena-single-ring.md` — the human acceptance
   checklist and controls, and the single-arena runbook (fix, assertions, failure triage).
5. `Match\RKCGameMode.cpp` — the centre banner's clock. `UpdateBanner()` advanced
   `BannerAgeSeconds` by `Sim.FixedTickSeconds * 0.0f`, a no-op, so the age never left zero and the
   `BannerAgeSeconds > 0.8f` branch that clears the banner was unreachable: once the countdown
   ended, `FIGHT!` and its band stayed on screen for the rest of the round. The age now advances by
   the frame delta, the same clock the event feed is aged by, clamped at zero. See K11.

A line-by-line static review of (1)–(3) found no defect (the console-manager walk, the
`FParse`/`FString::Split` handling of `-rkc.<Name>=<Value>`, the `FEngineVersion`/`FDateTime`/
`FFileHelper` calls and the include set all match their UE 5.8 signatures), but **a static review
is not a compile**, and nothing here has been compiled.

**Blocking detail (environment, not the project).** Every form of command execution attempted
this session was refused — plain command, interactive PTY, background/detached command, reuse of an
already-open session, and a delegated child process — with the platform message *"No active targets
are saved in this engagement."* No compiler, packager, or game run therefore executed. Nothing in
this section is a build result: it is the absence of one.

**Consequence (arena).** The single-arena requirement is therefore *implemented in source and
unverified in a binary*. The acceptance criterion for it is a packaged run with screenshots at round
start, during gameplay and after a rematch, compared against the supplied image, plus the self-test
record showing 0 failed checks and `0 of N greybox piece(s) visible`. The runbook is
`docs\09-arena-single-ring.md` section 5; nothing in that section has been executed.

**Consequence.** Item 1 (repackage), item 2 (rerun of override-dependent checks at the binary
level), item 4 (arena A/B), item 5 (black-fighter and audio) and item 6 (touch-UI visibility) stay
unverified at the binary level until a build runs on a machine where execution is permitted. The
existing `Packaged\Windows\RockyKombat.exe` predates the front end, audio, touch, arena and
build-stamp work and **must not be used for acceptance testing** (it is also the build whose
`BuildStamp.txt` does not exist).

### K11 — The centre banner never aged, so "FIGHT!" stayed up for the whole round (RESOLVED 2026-10-19 — see the status update at the end of this document)

Found by reading the source, not by running anything, and **not verified in any build**.

`ARKCGameMode::UpdateBanner()` ended with

```cpp
BannerAgeSeconds += Sim.FixedTickSeconds * 0.0f; // placeholder to keep age explicit
```

which adds nothing, and nothing else in the project writes `BannerAgeSeconds` except the resets in
`SetBanner` (only when the text changes) and the one in the `Fighting` case. Its only reader is the
clear branch:

```cpp
else if (BannerAgeSeconds > 0.8f)   // cannot become true while the age stays at zero
{
    SetBanner(TEXT(""), false);
}
```

So after the countdown the `FIGHT!` call was set on the first fighting frame and re-set on every
frame after it — the `else` branch deliberately preserves the age — and it stayed until `RoundEnd`
replaced the text.

**Visible effect:** `ARKCHUD::DrawBanner()` draws whenever the banner text is not empty, at
`CanvasH * 0.30`, with a full-width band behind it, and it does not read the age at all. The band
and the `FIGHT!` caption therefore stayed over the arena for the remainder of each round instead of
clearing 0.8 s after the countdown.

**Fix** (`Match\RKCGameMode.cpp`, end of `UpdateBanner()`): the age now advances by the frame delta,
matching the event feed a few lines below it (`Line.AgeSeconds += DeltaSeconds`), clamped at zero:

```cpp
const double BannerDelta = GetWorld() ? GetWorld()->GetDeltaSeconds() : Sim.FixedTickSeconds;
BannerAgeSeconds += FMath::Max(0.0f, static_cast<float>(BannerDelta));
```

**Why the frame delta and not the simulation step:** the banner is presentation, and one frame may
advance several whole ticks, one tick or none, so a fixed-step increment would make the 0.8 s depend
on the frame rate. **Known limitation of the fix:** `AdvanceMatchFrame()` receives its own
`DeltaSeconds`, and the self-test drives that loop directly; in a self-test run the world's delta and
the loop's delta need not be the same number, so the banner may clear early or late *in that run
only*. No self-test assertion reads the banner age, so nothing in the record depends on it.

**Not verified:** nothing has been compiled or run since the change (K10), and no assertion covers
the banner clearing. It needs the human play test and the arena acceptance run, by eye.

## Remaining work, in order

1. ~~**Finish gate A.**~~ **Done 2026-10-17.** `BuildCookRun` archived
   `Packaged\Windows\RockyKombat.exe` (BUILD SUCCESSFUL, ExitCode=0), and the packaged binary was then
   run through the same self-test:
   ```bat
   Packaged\Windows\RockyKombat.exe -nullrhi -log -unattended -stdout -RKCSelfTest=2 > packaged_selftest.log 2>&1
   ```
   Exit code 0, and the record
   `Packaged\Windows\RockyKombat\Saved\Evidence\RKC_SELFTEST_20261017_193618.txt` reports
   `failed checks: 0`, `result: PASS`, with the same match checksums as the editor build
   (0x817E602C, 0xEC4A3020) and the same per-match tick counts. Acceptance condition met.

2. **Close the HUD gap (gate B presentation) — done 2026-10-17, with named limits.** A windowed run of the packaged build
   (`Packaged\Windows\RockyKombat.exe -windowed -ResX=1280 -ResY=720`, captured with
   `Tools\capture_window.ps1`) rendered on this host, so K1 did not reproduce this time. The frames
   are `Saved\Evidence\packaged_shot_early.png` (start-of-round, both fighters apart) and
   `Saved\Evidence\packaged_shot_clinch.png` (mid-round). They show the health and stamina bars, the
   `ROUND 1 OF 3` counter, the timer, both fighter names, the event feed and the control-hint footer,
   plus both imported fighter sprites and the imported arena backdrop.
   Added later the same day (04.D3, 04.D4): one windowed self-test run asserts the HUD's own counters
   (167 checks, 0 failures, `drawn frames 5, layout regions 6, canvas 1280x720`), and seven stage
   screenshots — startup, loading, menu, versus, match, results and the touch pad — were taken by the
   game itself, written with a manifest, and then verified at the pixel level by
   `Tools\Diagnostics\verify_stage_shots.py` (7 of 7). Still open: no comparison against reference
   images, and no human has played it (item 3).

3. **Human-input play test (gate B completeness).** Launch, play one round with the keyboard
   bindings from `Config\DefaultInput.ini`, and confirm move/jab/heavy/block/dodge/rematch and the
   frame-data toggle all respond, then note which bindings felt wrong.

4. **Gate C, when art arrives.** Scripted Paper2D import of the sprite pack, sprite/flipbook assets
   for both fighters and the ring, and a side-by-side review against the references.

5. **Gate D, when the environment allows.** Install the Android SDK/NDK, pick a device, add a mobile
   rendering path and input scheme, and measure frame time. This is a large download on a machine
   with ~85 GB free, so confirm scope before spending the space.

6. **Then, and only then, gates E–H** (networking, identity/persistence, rankings, later modes).

7. **Verify the single-arena correction in a packaged build** (independent of gates C–H, and the
   highest-value outstanding item). Rebuild, repackage, run `-RKCSelfTest=3` from the packaged
   binary and read the record, then photograph the packaged game at round start, mid-round and
   after a rematch and compare with the supplied image. Full runbook:
   `docs\09-arena-single-ring.md` section 5, failure triage in section 6.

## Unverified claims to avoid repeating

- "The HUD works" — wiring, draw pass, a windowed assertion run and a pixel check of seven named
  screens all pass. What is *not* verified is that the layout looks right to a person, and the stage
  frames were taken with the touch pad forced on, so they are not a normal run's clean menus (K6, 04.D4).
- "The game is packaged" — now true and evidenced (see remaining work 1).
- "Controls work" — still unverified by a human. Bindings are read from `Config\DefaultInput.ini` and
  bound in `ARKCPlayerController`, but no key has actually been pressed (04.E.3).
- "The light warning is fixed" — the warning is absent from the packaged frame, but the effect was
  not isolated (K8).
- "The duplicate ring is gone" — the switch and the fallback are now editor-only and a packaged
  build refuses a request to show the ring, but **no build contains this change yet**, so exactly
  one visible arena has not been observed in any packaged binary. The claim requires section 5 of
  `09-arena-single-ring.md` (K10).
- Anything attributed to the product brief — it was never read and is not on this machine (K5).
- "The `FIGHT!` banner clears after the countdown" — the clock bug that stopped it clearing is fixed
  in source, but no build contains the fix and nothing asserts it (K11).


## Status update, 2026-10-19 — this supersedes K10, K11 and three bullets below

Command execution came back on this host. The editor target was rebuilt (2026-10-18 22:17, then
2026-10-19 18:27 — 7 of 7 actions, `Result: Succeeded`) and a fresh Win64 package was staged on
2026-10-19 18:34 (`Packaged\Windows\RockyKombat.exe`, `Manifest_*_Win64.txt` all stamped 18:34).
Everything below is read out of records that the packaged binary itself wrote.

**K10 — resolved.** All four source changes are in the shipped binary, and the records prove it
rather than assuming it, because the assertions they contain can only be printed by the new code:

- `Packaged\Windows\RockyKombat\Saved\Evidence\RKC_SELFTEST_20261019_190218.txt` (headless, 200 checks,
  0 failed) carries `rounds owed: 4 round-start cue(s), 0 round-end cue(s) (4 round(s) started,
  0 broke; a knockout round breaks nothing)` — the round-cue accounting added on Oct 19 — and the
  arena block `arena: 0 of 27 greybox piece(s) visible | picture 2981 x 1677 cm, aspect 1.7768 vs
  image 1.7768 | feet/floor delta 0.00000 | narrowest cover margin 18 cm`.
- `...\RKC_SELFTEST_20261019_190606.txt` (windowed, 203 checks, 0 failed) adds
  `roster swap: side A presents the new character's art (showing FB_Shadow_Knockdown, expected a
  FB_Shadow_ clip)` — the rematch-artwork fix executing on the reused actors — and asserts the HUD
  really drew (5 draw passes, 6 regions, canvas 1280x720).

**Arena acceptance (remaining work 7) — done in a packaged build.** The binary was rebuilt,
repackaged and run through `-RKCSelfTest=3`; the records above report `0 of 27` procedural pieces
visible while the painted ring is presented, the ring hidden by its own report, one ring actor in the
world, the picture's aspect preserved, and the painted floor line under the fighters' feet. The
shipped binary was then photographed by its own capture driver at 7 of 7 stages
(`Saved\Evidence\Stages\20261019_191111\manifest.txt`, pixel-verified 7 of 7 by
`Tools\Diagnostics\verify_stage_shots.py`).

**K11 — the fix is in the shipped binary, the behaviour is still not asserted or watched.** The
banner clock lives in the same translation unit as the accounting code proven above, so the 18:34
package contains it. What it still lacks is an assertion (nothing reads `BannerAgeSeconds`) and a
human who watched `FIGHT!` disappear 0.8 s after the countdown. Keep it on the play-test list.

**Remaining work 3 — the shipped build has still never had a person at the keyboard, and this
section previously said otherwise.** Three records exist from the 19:13–19:18 window on the 18:34
package (`RKC_20261019_191339/191749/191803_match-end.txt`) and were first written up here as an
interactive playthrough. They are not one, and the records say so themselves.
`RKC_20261019_191803_match-end.txt` reports **20 screen change(s), 0 accept(s), 0 tap(s), 0 option
change(s)** — the entire front end
(`Menu -> Experience -> Arrangement -> CharacterSelect -> Difficulty -> Confirm -> Loading -> Versus
-> Match -> Results -> Menu`) advanced with no accept press reaching the machine, which only the
scripted menu driver can do: `FMachine::TickAutoPilot` calls `ActivateItem` directly and never
touches `Diag.Accepts`, while every human path (key, pad, tap) increments one of those counters. The
launch log settles it: that run started with `-unattended` and `-rkc.FrontEndAuto=3`
(`Packaged\Windows\RockyKombat\Saved\Logs\RockyKombat-backup-2026.10.19-18.18.03.log` line 297,
sha256 `877a1b09…`, override confirmed at line 840). `bot A: human` in the record means the side was
*configured* as the player's side, not that anyone was playing it: that match ended
`RESULT ROCKY BY KNOCKOUT | 1 round(s) | 1193 ticks (19.88 s)` with **0 attacks from side A and 13
from the CPU**.

The two signatures — a complete front-end flow with 0 accepts, and 0 attacks from the
human-configured side — appear in all 24 records written from 18 October onwards (of the 28 match
records in the folder, the other four are the 17 October ones). The only records
in the evidence folder where the player-controlled side actually produced input are the two from the
older 17 October build: `RKC_20261017_195630_match-end.txt` (2 attacks, 1 hit, 3 dodges) and
`RKC_20261017_195736_match-end.txt` (1 attack), neither of which has a front-end section at all.
Both records are re-quoted with their line numbers in the excerpt
`RKC_20261019_191803_launch-log-excerpt.txt`, which now ships inside the pack beside the record it
explains. No keystroke from a person is recorded against the shipped build, so hit feel, blocking,
dodging and the rematch loop rest on the automated checks alone, and the 8-point checklist in
`docs\08-playtest-checklist.md` stays unticked.

**Remaining work 5 (Android) — toolchain finished, engine payload still missing.**
`Scripts\validate_android.bat` now prints `##PlatformValidate: Android VALID r27c`. The refusal is a
single engine marker file (`Engine\Binaries\Android\UnrealGame.target`, named by
`Engine\Config\BaseEngine.ini` lines 4008–4013); the exact launcher click-path and evidence are in
`docs\12-safearea-and-android-status.md` section 4.

**Distribution.** `Dist\RockyKombat-Win64-1.0.0.zip` was rebuilt after the "human playthrough"
correction and re-verified by extracting the shipped archive (452,883,760 bytes, 181 files, sha256
`d4c67c861ff29890141a1a7fb9ae9ab16d108bba6acdf8ebe9ac417e0da420ca`): 180 of 180 files matched
`SHA256SUMS.txt`, the guide checker passed 9 of 9, and the extracted copy ran its own self-test to
219 PASS / 0 FAIL. Details and hashes: `docs\13-distribution-pack.md`.

### Corrections to the "unverified claims" list below

- *"The duplicate ring is gone"* — no longer pending. A packaged binary now reports 0 of 27
  procedural pieces visible and refuses to draw the ring, and the shipped build's stage frames were
  pixel-verified. The claim is supported for Windows.
- *"The `FIGHT!` banner clears after the countdown"* — the fix is in the shipped binary now, but the
  clearing is still neither asserted nor observed by a person. Still not a supported claim.
- *"Controls work"* — still unsupported. An earlier draft of this section claimed a human drove the
  front end and a match on the shipped binary; that was wrong (see remaining work 3 above: the run
  was `-unattended -rkc.FrontEndAuto=3`, and its record shows 0 accepts and 0 attacks from the
  player's side). What is verified is the wiring, not the keystroke: the bindings register at
  runtime in the packaged build, and the 17 October records show player-side input reaching the
  simulation on that older build (2 attacks, 1 hit, 3 dodges). The five combat verbs on the shipped
  build remain unverified by hand.
- *"The HUD works"* — unchanged: automated draw assertion and pixel checks pass; nobody has judged the
  layout by eye in a normal (touch-pad-off) run.


---

## Increment of 2026-10-19 20:00 — three closed questions, one new cosmetic finding

Written after rebuilding, repackaging and re-verifying the shipped binary (staged 20:07).
The packaged self-test now reports **219 checks / 0 failed** headless and **222 / 0** windowed
(up from 200 and 203), because this increment added checks rather than fixing failures.

### 1. Closed: the self-test could report PASS with the artwork missing

*Found earlier while blocked, fixed now.* Every arena and fighter-artwork assertion sat inside
`if (RKCArt::IsArtAvailable())`, which is deliberate — a clean source checkout has no imported
art and must still pass — but it also meant that a *cooked* build with the art missing skipped
the whole block and still printed a full PASS. The one failure the checks exist to catch was
the one failure they could not report.

A cooked build is now required to carry the art (`WITH_EDITOR` decides which rule applies), so
the record contains an explicit check for it:

```
PASS  a cooked build carries the imported artwork (art resolved)
```

In the editor the old behaviour stands, because that is where a checkout without content is
normal.

### 2. Closed: the "fighters turn black" report has a measured explanation

*Still unreproduced, but no longer unexplained.* Two mechanisms could have produced it, and
both are now measured instead of argued about:

- **The fighters are drawn with an unlit material.** The self-test reads the material the
  sheet actually shades with at runtime and prints it in its evidence record:

  ```
  fighter A sprite material: /Paper2D/MaskedUnlitSpriteMaterial.MaskedUnlitSpriteMaterial
  fighter B sprite material: /Paper2D/MaskedUnlitSpriteMaterial.MaskedUnlitSpriteMaterial
  PASS  fighter A draws with an unlit sprite material (found …MaskedUnlitSpriteMaterial)
  PASS  fighter B draws with an unlit sprite material (found …MaskedUnlitSpriteMaterial)
  ```

  An unlit sprite cannot be darkened by a light, a shadow or a 180-degree turn. (The project's
  own `M_RKC_SpriteMasked` is not what the renderer uses: the importer set it as each
  *sprite's* default material, while `UPaperFlipbookComponent::GetMaterial` prefers the
  *flipbook's* `DefaultMaterial`, which is the engine's `MaskedUnlitSpriteMaterial`. Both are
  unlit, so the appearance is the same and nothing was changed on the basis of this — the
  finding is recorded because it explains why the material appears in two places.)
- **The fighters cast no shadows.** `CastShadow` and `bCastDynamicShadow` are both off on the
  sprite component, so two fighters cannot shade each other.
- **Both fighters were measured in the shipped match frame.** The body of the left fighter
  reads `247,148,29` and the right `250,152,34` — equally bright, although the two face
  opposite ways. Two independent decoders (PIL and .NET `System.Drawing`) returned the same
  values for the same probe points, so this is the file's content, not a viewer's opinion.

`KNOWN_ISSUES.txt` in the pack now carries this evidence, and the report stays listed as
"unreproduced" rather than closed.

### 3. Closed: application lifecycle is now asserted, not assumed

The handlers existed (`HandleApplicationBackgrounded` / `HandleApplicationForegrounded`, bound
to the core background/foreground and deactivate/reactivate delegates plus Slate activation)
but nothing had exercised them. The self-test now simulates a full background/return with real
input held: two fingers on the on-screen pad positioned from the pad's own layout (one owning
the stick and pushed off centre, one on the guard) plus a keyboard latch with a movement key
held, the guard held and one punch still pending. It then asserts:

- the precondition that something really was held (2 fingers, stick and guard active, ≥1
  latched press);
- after backgrounding: no held input of any kind, the pending press dropped, every finger
  cancelled rather than left hanging, the pad producing a neutral state, and **one** freeze
  request recorded at the audio layer with no music stop or restart;
- after the return: input still clear, the music resumed, and **one** resume request recorded
  — so a key held across the gap is not a press and cannot fire an attack into a match nobody
  was watching.

Two supporting changes made that measurable: `ARKCPlayerController::IsAnyHumanInputHeld()`
(held state as well as edge counts, keyboard and pad) and `FRKCAudioDiagnostics::PauseRequests /
ResumeRequests`, which count the transitions the game asks for even when no audio device is
live — in a self-test run there is none, so `MusicPauses`/`MusicResumes` stay at zero and
*those* numbers would have proved nothing. The record prints both pairs.

### 4. Corrected 2026-10-20: the "tiled MIDDLEWEIGHT wordmark" was the fighter nameplate, not the artwork

This section previously claimed that the arena PNG carried a faint, repeated "MIDDLEWEIGHT"
wordmark baked into the picture, and that the fix would be an art edit. **That was wrong**, and
it is corrected here rather than quietly deleted.

The text on the floor was drawn by the game: `ARKCFighter::NameText` was a
`UTextRenderComponent` whose text is `"<name>\n<division>"` (for Rocky: `ROCKY` over
`MIDDLEWEIGHT`), attached to the fighter's *root* — i.e. at ground level between the two boxers,
not above their heads — and it was drawn twice per match, once per fighter. Three independent
measurements settle it:

- Crops of the source PNG at the two positions the nameplates occupy in the shipped frame
  (`Saved\Evidence\wordmark_crop_L.png`, `wordmark_crop_R.png`) show plain painted floor.
- A full-width band of the arena floor (`Tools\Diagnostics\rkc_arena_text_check.py`, output
  `Saved\Evidence\arena_floor_band.png`, 1672x376) contains no lettering at all; its 11.21%
  "strong edge" share is plank seams and paint texture, not glyphs.
- The earlier "tiled" reading came from an autocorrelation scan whose dominant period (48 px)
  was the ring railing and rope lines, not any text. The scan measured the room, not a wordmark.

So the finding is not an art defect and needs no repaint. The component itself was the defect;
see section 6, which removes it.

### 5. Closed: no black screen reaches the player before the first UI frame

The open question was whether a black frame could ever be *shown* — behind the loading card or
between presenting the window and presenting the first UI. Three bursts of the packaged build,
each launched by `Tools\Diagnostics\rkc_blackwatch_capture.ps1` so capture starts at process
launch, were measured with `rkc_blackwatch_analyze.py` then located in time with
`rkc_black_sequence.py`:

| Capture | Frames | Cadence | Black frames | First lit | Black after first lit |
|---|---|---|---|---|---|
| `20261019_203836` | 126 | ~630 ms | 7 (1–7) | 8 | 0 |
| `20261020_130441` | 45 | ~440 ms | 21 (1–21) | 22 | 0 |
| `20261020_130553` | 55 | ~327 ms | 31 (1–31) | 32 | 0 |

In all three, **every black frame is a prefix**: not one black frame appears after the picture
has been up, so no black screen was ever displayed once the game had drawn. Two further
properties make the prefix a capture artefact rather than a black window the player saw:

- **The black frames are one image.** Every leading black frame in all three runs shares a single
  SHA-1 (`29f74898ee322526`) — the window's empty surface repeating, not changing game content.
- **Nothing dark follows.** With a far stricter cut than the analyser's own threshold of 8, no
  frame after the first lit one falls below mean 20 in any run (lowest observed: 29.26, 36.94,
  36.94). There is no fade-from-black either.

The first lit frame is the branded card, not merely "something brighter": it measures
`frame_mean 36.99, backdrop 42.38, fighters 38.72 / 39.14` in **all three** runs, pixel for
pixel, and shows "ROCKYCAT ECOSYSTEM presents" over the night arena with both fighters visible.
That is structural as well as measured — the machine's first screen is `EScreen::Boot`, whose
heading is `ROCKYCAT ECOSYSTEM` (`Source\RockyKombat\UI\RKCFrontEnd.cpp:1422`), so UI is present
from the first presented frame, and no fade-from-black overlay exists in the HUD.

**Measured limitation, stated rather than hidden:** `PrintWindow` costs 327–630 ms per grab on
this host, so the sweep cannot exclude a black flash shorter than roughly 0.3 s between two
samples. It does exclude a sustained black screen, a black screen behind the loading card, and a
fade through black. Ruling out a single-frame flicker would need a per-rendered-frame recorder
inside the engine; nothing in the build records per-frame luminance today.

Verdicts are stored with each capture as `black_sequence_verdict.txt` beside `analysis.json`.

### 6. Fixed: the world-space nameplate drew a black mass over both fighters (2026-10-20)

**What the player saw.** In every shipped match frame a black shape sat on the floor across both
fighters' legs: `ROCKY` over `MIDDLEWEIGHT` rendered as black glyphs with a heavy black shadow,
instead of the gold (255, 196, 120) and light blue (150, 172, 255) the code asked for. On a dark
arena it read as a smear over the artwork.

**Why it was drawn there at all.** The component was attached to `Root` and never given a
relative location or a height, so it stood at Z = 0 between the fighters' feet. It was also the
lowest-risk possible piece of the presentation: nothing in the simulation depends on it.

**Why it came out black.** The colour is applied as vertex colour through the engine's default
text material. In this cooked build the text draw resolved to a dark, shadowed mass — the colour
request never reached the screen, and the guideline/outline around each glyph showed as the
black "box" the probe measured. The exact internal reason was not isolated further, because the
component was already redundant: the HUD names both fighters in its side panels and in the
centre scoreboard (`RKCHUD::DrawFighterPanel`, `RKCHUD::DrawCentreHeader`).

**What was changed.** `ARKCFighter` no longer creates the component or references it; the
member, the constructor block, the per-roster colour assignment, the visibility part count and
the per-frame `SetWorldRotation` call are all gone (`Source\RockyKombat\Fighters\RKCFighter.h`,
`.cpp`). The greybox rig, the artwork path and the hitbox debug draw are untouched.

**Measured before and after** (`Tools\Diagnostics\rkc_nameplate_probe.py`, region
(260,430)-(350,470) of the match frame, which is where the left nameplate sat):

| | pre-fix `Stages\20261019_184243\05_match.png` | post-fix `Stages\20261020_133731\05_match.png` |
|---|---|---|
| near-black pixels (lum < 12) | 1794 / 3600 = **49.83%** | 623 / 3600 = **17.31%** |
| mean of darkest tenth | rgb 0.0, 0.0, 0.0 | rgb 4.8, 5.3, 40.0 |

17.31% is the painted floor's own dark plank and shadow detail, not glyph mass; the new frame
shows the fighters' legs unobstructed.

**Verification of the rebuild.** Editor build `Result: Succeeded` (6 actions, 140 s); headless
3-match self-test **PASS, 0 failed checks** (`Saved\Evidence\RKC_SELFTEST_20261020_133009.txt`);
Win64 package `BUILD SUCCESSFUL` (5m 49s); packaged self-test **PASS, 0 failed checks**
(`Saved\Evidence\RKC_SELFTEST_20261020_133708.txt`); and a windowed stage capture taken by the
shipped binary itself (`Saved\Evidence\Stages\20261020_133731`, 6 of 7 stages, the touch-pad
stage being expected-missed on a desktop run without `-rkc.TouchForced=1`).

**Not claimed:** that the nameplate can never be wanted. It is removed because it was legible to
nobody and sat on top of the artwork; if a world-space nameplate is wanted later it should be
placed above the head and drawn through the HUD canvas, where colour and font are already known
to work, rather than through an engine text material in a cooked build.

### Remaining work after this increment

- **Human acceptance.** Unchanged and still the largest gap: checks 1–5 and 7–8 of
  `docs\08-playtest-checklist.md` need a person at the keyboard. Hit feel, blocking, dodging
  and the rematch loop have automated coverage only.
- **Android.** Unchanged: engine-side component, one launcher click, see `docs\12`.
- **Audible output by ear.** Unchanged: the records prove the cues were planned, issued and
  accounted for; a log line is not a speaker.
- **Design questions**, unchanged: the roster is two boxers from the supplied pack (the Panther
  martial artist has no matching state machine), and there is no jump mechanic or jump art.

## Increment of 2026-10-20 18:40–19:05 — input fix verified in the shipped build, one rendering defect reproduced

### 1. Closed: the packaged build now maps every control

The repackage of 18:21 baked the corrected `Config/DefaultInput.ini` (38 of 38 mapping lines
carry the `+` prefix, 0 bare `Key=` lines, SHA-1 `08A623549071825197C6A33328867F59A04CCD13`)
into `RockyKombat-Windows.pak`. The windowed runs that followed stalled on the UHD620 before
the match ended, so the authoritative record was taken with a rendering-free (`-nullrhi`) run of
the same shipped executable:

- `-rkc.Script=playthrough -nullrhi`, exit code 0.
- Record: `Packaged\Windows\RockyKombat\Saved\Evidence\Scripted\20261020_184035_script-playthrough.txt`
- 40 of 40 steps report `mapped`, 0 `UNMAPPED`, `accepts=10`, `navigates=9`.
- Full screen walk: Menu → Experience → Arrangement → CharacterSelect → Difficulty → Confirm →
  Match → Results → Menu → Quit. Every match step reports `CONSUMED`.
- 146 events dispatched, 135 delivered, 11 loop repeats, 2 blocks skipped.
- Log: `C:\RockyKombat\packaged_nullrhi.log`.

Reading note: the `Enter` steps show `NOT CONSUMED` at the probe even though the screen does
advance, because the probe samples immediately after `InputKey` returns and the accept is latched
on the following tick; the `accepts`/`screen` columns on the next line are the proof that the
press acted. Movement, jab, heavy, block and dodge steps all report `CONSUMED` directly.

### 2. New, reproduced: a black duplicate of a fighter stands in the ring after the front-end flow

Observed in two independent windowed runs launched through the front end
(`acceptance_20261020_1850.mp4`, `acceptance_v2_20261020_1900.mp4`): next to each living fighter
stands a second, pure-black copy of that same sprite, offset roughly 50–80 px to its left and
standing upright on the ring floor, sharing the fighter's pose family (guard, lunge, victory).
The textured fighter renders correctly in the same frame; only the copy is black.

- Present from the first second of the match and through the whole match, in both takes.
- **Not** present in a direct match started with `-rkc.FrontEnd=0` on the same binary
  (`probe_directmatch_14s.png`, `probe_directmatch_17s.png`), where Rocky and Shadow both render
  fully textured with no copy.
- So the trigger is the front-end → match transition, not the asset set: the log shows
  `[RKCArt] Rocky art set: 8/8 flipbooks` and `[RKCArt] Shadow art set: 8/8 flipbooks`, no art or
  material warnings, and `ARKCFighter` creates exactly one `UPaperFlipbookComponent` named
  `Sprite` with no tint, no second sprite component and no shadow component.
- Mechanism unconfirmed. The remaining candidates, none yet proven: a leftover showcase/versus
  actor from the front end that is never destroyed, and a translucent/masked shadow pass drawing
  the sprite's silhouette in black.
- Evidence: `Saved\Evidence\Recording\v2\zoom_f60.png` (panther plus its black copy),
  `Saved\Evidence\Recording\frames\zoom_t140.png`, `frames\seq80.png`.

A separate, smaller artifact appears in **both** modes: a solid black quad roughly 30×40 px
standing on the ring floor between the fighters (`probe_directmatch_17s.png`, centre of the
ring; also visible at 14 s). Same family as the removed nameplate: a world-space component that
resolves to black in a cooked build and is not needed, because the HUD already names both
fighters.

### 3. Acceptance recording produced, with stated limits

`Saved\Evidence\Recording\acceptance_v2_20261020_1900.mp4` — 140 s, 1920×1080, H.264 + AAC
(room audio through the microphone array, mean −26.6 dB, peak −7.0 dB). It shows character
select, confirm, the match with the CPU moving and attacking, the results card and the return to
the main menu.

Limits, so nobody reads more into it than it shows: the host renders this scene at roughly
2–4 fps (measured from the shipped binary's own frame counter), so the capture holds only ~4.4
fps and the motion is visibly stepped — that is the UHD620, not the game's simulation. The
built-in `playthrough` script selects Rocky as the human side, so this take demonstrates a
human-driven Rocky against a CPU Shadow, not the Shadow-controlled run the checklist asks for.
