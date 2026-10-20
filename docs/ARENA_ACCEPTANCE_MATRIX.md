# Arena correction — acceptance matrix and evidence status

Scope of this document: the mandatory correction "the supplied Midnight Rooftop Ring
image is the only visible arena", plus the packaging and screenshot proof it requires.

Status vocabulary used below:

- **CONFIRMED ON DISK** — the change was re-read verbatim from the source file on
  2026-10-18. This proves the code is present; it does **not** prove it compiles, and it
  does **not** prove the packaged game behaves differently.
- **NOT DONE** — the step needs a compiler, a packaged binary or a person, and could not
  be performed.

## 1. Your requirements, one by one

| # | Requirement (your words) | Source-side change | Automated check | Human check | Evidence today |
|---|---|---|---|---|---|
| 1 | Remove/disable every duplicate procedural ring visual: ropes, posts, turnbuckles, canvas, apron, ringside floor, placeholder backdrop, decorative geometry | Greybox visibility is compile-time gated; `SetGreyboxVisible(true)` refused and logged in packaged builds | Packaged self-test (`-RKCSelfTest=3`) prints and asserts the visible-piece count must be 0 | No second ring in versus card, round start, gameplay, knockdown, results, rematch | **CONFIRMED ON DISK** — not built, not run |
| 2 | Preserve only invisible gameplay elements for collision, positioning, boundaries | Pieces kept in the world hidden by a visibility flag; they never carried collision. Bounds are `FMatchRules.ArenaMinX/ArenaMaxX` in `RKC::FCombatSim` | Self-test asserts ring geometry still exists while visible count is 0 | Fighters still stop at the ropes and cannot leave the ring | **CONFIRMED ON DISK** — not run |
| 3 | No duplicate ring during startup, introductions, gameplay, knockdown, results or rematches | One visibility predicate, no per-stage bypass | Self-test repeats the assertion after **every** restart (`RKCGameMode.cpp` ≈2812–2822) and at end of run (≈2952–2960) | Walk all six stages in one session, including a rematch | **CONFIRMED ON DISK** — not run |
| 4 | Supplied-image presentation is the default in the packaged game; no launch command or console setting needed | `rkc.ArenaGreybox` compiled out of every packaged target (`RKCArena.cpp` 41–58); `GreyboxRingRequested()` returns `false` there (60–70) | Launch the packaged exe with **no** `-rkc.*` switches and read the record | Same launch, no console use | **CONFIRMED ON DISK** — not run |
| 5 | Greybox fallback development-only and disabled during normal play | `WITH_EDITOR` gate on the CVar and on `GreyboxFallbackAllowed()` (72–84); packaged refusal at 383–400 | `-rkc.ArenaGreybox=1` in the packaged build must be logged as refused | Optional negative test | **CONFIRMED ON DISK** — not run |
| 6 | Frame the artwork correctly: preserve aspect ratio | `GetImageAspect()` added (363–369); width and height are both derived from the single number `ArenaImageAspect` (1672 × 941) | Self-test compares picture aspect to image aspect (2751–2754); the analyzer reports each capture's aspect | Compare silhouette proportion against the artwork | **CONFIRMED ON DISK** — not run |
| 7 | Align both fighters' feet with the painted fighting floor | Floor line projected onto the screen line the feet project onto | Self-test asserts feet/floor delta ≤ 0.001 (2755–2758) | Feet on the painted mat in idle, hit, knockdown, corner states | **CONFIRMED ON DISK** — not run |
| 8 | No empty space below or around the image, achieved by camera framing and background sizing — not by adding another visible floor or ring | Cover-fit scaling with `FramingOverscan` (5 %); no extra visible floor actor | Self-test asserts narrowest cover margin > 0 cm (2759–2762); analyzer M3/M4 check the band below the feet | Letterbox-free at every stage | **CONFIRMED ON DISK** — not run |
| 9 | Check supported window sizes and mobile aspect ratios | `UpdateArenaFraming()` re-frames on every window-shape change; windows narrower than 4:3 render as a 4:3 view with bars beside | Re-run captures at 1920x1080, 1280x720, 1024x768, 1080x1920 and read the logged aspects | Spot-check portrait framing and touch HUD overlap | **NOT DONE** |
| 10 | Verify the actual packaged build; check stale maps, duplicate spawned actors, config overrides, old packaged content | n/a | `Tools\Diagnostics\rkc_arena_verify.bat` rebuilds, repackages, runs the packaged self-test, extracts the record | Open the newest packaged exe and look at it | **NOT DONE** |
| 11 | Provide screenshots at round start and during gameplay showing the complete arena and both fighters, compared against the supplied image | n/a | Analyzer consumes the captures; the comparison itself is human | Side-by-side against `Midnight_Rooftop_Ring.png` | **NOT DONE** |
| 12 | Acceptance: exactly one visible arena — the approved image — with fighters and HUD over it; a source-code visibility change alone does not establish the fix | n/a | n/a | Final verdict | **NOT MET** |

## 2. Why nothing is verified

Command execution is refused at the platform level. Every attempt returns:

```
No active targets are saved in this engagement.
```

This is not an approval problem and not a permission problem. File access to the project
works (the source reads and the file writes in this task prove it). The **execution**
channel requires a saved engagement target, and none is recorded, so no process is
started: no compile, no cook, no package, no game launch, no screenshot.

A source-code read is not a compile. A compile is not a packaged run. Only a packaged run
answers the question you actually asked.

## 3. What to run on the Windows host

Files placed in the project by this task:

| File | Location | What it is |
|---|---|---|
| `rkc_arena_verify.bat` | `Tools\Diagnostics\` | rebuild + repackage + packaged self-test + capture prompts + analysis |
| `rkc_arena_verify.py` | `Tools\Diagnostics\` | stdlib-only PNG screen for the duplicate-ring signature |
| `ARENA_ACCEPTANCE_MATRIX.md` | `docs\` | this document |

The `.bat` invokes the `.py` from its own directory, so keep the two together.

```
cd C:\RockyKombat
Tools\Diagnostics\rkc_arena_verify.bat
```

Optional overrides if auto-detection fails:
```
set RKC_PROJECT=C:\RockyKombat
set RKC_UE_ROOT=C:\Program Files\Epic Games\UE_5.8
```

The harness does, in order:

1. Locate `RockyKombat.uproject` and the UE 5.8 install.
2. Build `RockyKombatEditor Win64 Development`, then cook/stage/package a fresh Win64
   Development build with `BuildCookRun` into `Packaged\`.
3. Launch the **newest packaged** `RockyKombat.exe` windowed with `-RKCSelfTest=3`, write
   the log to `Saved\Evidence\Arena\`, and extract from the record the failed-check count
   and the greybox visibility line.
4. Print the manual capture instructions.
5. Run `rkc_arena_verify.py` over every PNG in `Saved\Evidence\Arena\`.

Neither file has been executed, and the Python has not been syntax-checked by an
interpreter, because no process could be started. Treat the first run as a dry run and
read the output rather than assuming it works.

## 4. Sign-off rule

Met only when all of the following hold:

- packaged self-test record: `0 failed checks` **and** `0 of N greybox piece(s) visible`;
- analyzer: `M1..M4` pass for every captured frame, with the pre-fix control supplied so
  M2 is the relative test;
- your eyes on the packaged build: one arena — the supplied artwork — with both fighters'
  feet on the painted floor, no empty space below or around it, in every stage including
  a rematch and a portrait window.

The analyzer only looks for the greybox colour signature. It cannot confirm that the
visible ring *is* the artwork, nor that the feet sit on the painted floor line. Those
remain human checks.
