# 11 - Arena single-ring verification: RESULT

Built and verified on 2026-10-18 against a freshly packaged Win64 Development
build. This document records what was run, what it proved, and what it did not.

## Build under test

| Artefact | Timestamp (local) |
| --- | --- |
| Editor module `UnrealEditor-RockyKombat.dll` | 2026-10-18 23:07:31 |
| `Packaged/Windows/RockyKombat.exe` | 2026-10-18 23:13:40 |
| `Packaged/.../Content/Paks/RockyKombat-Windows.pak` | 2026-10-18 23:14:16 |

Package command: `Scripts\package_win64.bat` (BuildCookRun, `-build -cook
-stage -pak -archive`). Result: `BUILD SUCCESSFUL`, `AutomationTool exiting with
ExitCode=0`.

A first attempt failed with `A conflicting instance of AutomationTool is already
running` because another BuildCookRun was in flight. It aborted before touching
any output; the successful run above is the one that produced the binaries.

## Requirement 12 - one arena, the supplied image

Self-test record `Saved/Evidence/RKC_SELFTEST_20261018_231508.txt`, run from the
packaged binary with `-RKCSelfTest=3 -windowed -ResX=1280 -ResY=720` (no
`-rkc.*` switches, so the packaged defaults are what is under test):

```
headless:     no
--- IMPORTED ARTWORK (present) ---
arena: 0 of 27 greybox piece(s) visible | picture 2293 x 1290 cm,
       aspect 1.7768 vs image 1.7768 | feet/floor delta 0.00000 |
       narrowest cover margin 7 cm
PASS  nothing else is drawn around the imported image (0 of 27 greybox piece(s) visible)
PASS  match 1 restart: still one visible arena (0 greybox piece(s) visible, 27 built, was 27)
PASS  match 2 restart: still one visible arena (0 greybox piece(s) visible, 27 built, was 27)
PASS  match 3 restart: still one visible arena (0 greybox piece(s) visible, 27 built, was 27)
PASS  at the end of the run the arena is still one ring (0 greybox piece(s) visible, 27 built)
failed checks: 0
result:        PASS
```

`headless: no` matters: unlike the older `-nullrhi` records, this run had a real
renderer, so the HUD section is meaningful too - "drawn frames 5, layout regions
6, canvas 1280x720", all PASS.

The artwork-present line matters equally. The single-arena assertions sit inside
an `if (RKCArt::IsArtAvailable())` block, so if the arena texture were missing the
whole block would be skipped and the record would still say PASS. `present` is
what makes the rest of the block evidential rather than decorative.

## Pixel analysis of the captures

`Tools/Diagnostics/rkc_arena_verify.py`, run against the captures from the fresh
build, with the known pre-fix capture as the control:

| Frame | M1 rendered | M2 no duplicate ring | M3 no void band | M4 band textured |
| --- | --- | --- | --- | --- |
| 01_startup | PASS | PASS | PASS | PASS |
| 04_versus | PASS | PASS | PASS | PASS |
| **05_match** | **PASS** | **PASS** | **PASS** | **PASS** |
| 07_results | PASS | FAIL | PASS | PASS |

The match frame - the one this requirement is about - passes all four. For it,
`arena flat_sat = 0.000000` and the raw figure of 0.004322 is accounted for
entirely by the HUD rows.

### The one FAIL is a false positive, and here is the evidence

`07_results` fails M2 with `arena flat_sat = 0.005373`. Localising those pixels
puts all of them in a single box, x 388-891, y 246-268. That is the red
selected-row highlight bar of the results menu (`> REMATCH`), the same widget and
the same coordinates that fail on the main menu (`> PLAY`).

M2 exists to find untextured greybox ring primitives in the arena band of a
match frame. It is being applied to a front-end screen, where a flat saturated
UI highlight legitimately looks like one. This is a limitation of the checking
tool, not a property of the game.

**Recommended fix for the checker, not the game:** apply M2 only to match/arena
frames, or have it ignore rows belonging to a detected UI highlight. Until then,
treat an M2 FAIL on a non-match screen as unproven rather than as a defect.

## Roster artwork identity

A related defect was found and is now fixed in the build under test.

Older builds resolved fighter artwork once and kept it for the life of the actor.
In a match where both sides selected the same character, the reused actor kept
the previous character's flipbooks: the fighter was drawn as one character and
labelled, scored and hitboxed as another.

Evidence from the build under test:

```
[RKCArt] Rocky art set: 8/8 flipbooks
ROCKY is presenting imported artwork (8/8 flipbooks); greybox body hidden.
[RKCArt] Shadow art set: 8/8 flipbooks
SHADOW is presenting imported artwork (8/8 flipbooks); greybox body hidden.
   ... later, on the ROCKY vs ROCKY match:
[RKCArt] Rocky art set: 8/8 flipbooks
ROCKY is presenting imported artwork (8/8 flipbooks); greybox body hidden.
```

The second `Rocky art set` line is the fix working: the roster comparison detects
the change and re-resolves the artwork. The visual capture of that match shows
two identical tabby fighters, which is correct for ROCKY vs ROCKY.

## What is NOT verified

- **Human controls.** No person has pressed a key against this build. Every match
  in this evidence was driven by the self-test or the capture autopilot. That
  human-input round is still outstanding, and it is the point of the exercise.
- **Touch controls.** The `06_touchpad` capture was missed by the capture
  driver, because the touch pad is off by default on a desktop build. Pass
  `-rkc.TouchForced=1` to photograph it. The pad renders; it has not been
  photographed on this build.
- **Non-16:9 windows.** The arena framing is derived from the window aspect.
  Only 1280x720 was exercised here. A 4:3 window and a portrait window have not
  been measured against the artwork.
- **Long sessions and lifecycle.** Backgrounding, focus loss, pause/resume and
  stray-music behaviour were not part of this run.

## Sign-off state

Requirements 12's automated half is now satisfied by a freshly built binary with
artwork present, a real renderer, and a match-frame pixel analysis that passes.
The human half - a person looking at the screen and playing it - remains.
