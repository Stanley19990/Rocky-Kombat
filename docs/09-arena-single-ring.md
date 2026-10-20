# 09 — One visible arena: the supplied image, and nothing else

This document covers the mandatory arena correction: the approved *Midnight Rooftop
Ring* image must be the **only** visible arena, at every stage of play, in the packaged
game, with no launch command and no console setting required. Any procedural ring
(ropes, posts, turnbuckles, canvas, apron, ringside floor, placeholder backdrop,
decorative geometry) must not be drawn over, inside or beneath it. Anything kept for
collision, positioning or boundaries stays, but invisibly.

**Acceptance criterion, quoted from the request:** "exactly one visible arena — the
approved Midnight Rooftop Ring image — with the fighters and HUD placed over it. A
source-code visibility change alone does not establish that this is fixed."

That last sentence is why section 5 of this document exists: the change is only
accepted once a **packaged run** has been inspected and photographed.

---

## 1. What was wrong before

The procedural greybox ring was suppressed, but only by a runtime switch:

- `rkc.ArenaGreybox` was declared under `#if !UE_BUILD_SHIPPING`.
- `GreyboxFallbackAllowed()` returned `true` in **any** non-shipping build.

A packaged game is normally a **Development** build, where `!UE_BUILD_SHIPPING` is
still true. So in the build that actually ships:

- the console variable existed, and a stale `[ConsoleVariables]` entry in
  `Saved\Config\Windows\Engine.ini` could set it to `1` and bring the duplicate ring
  back over the image;
- if the imported image failed to load or resolve, the placeholder ring was allowed to
  stand in as the arena — a second, differently-shaped ring on screen.

The code was therefore correct only as long as nobody typed anything and the art
always loaded. That is not a fix.

## 2. What the code does now

The decision is made at **compile time** and is no longer reachable from the shipped
game, and it is additionally clamped at runtime (defence in depth).

| File | Change | Location |
| --- | --- | --- |
| `Source\RockyKombat\Arena\RKCArena.cpp` | `rkc.ArenaGreybox` and its `TAutoConsoleVariable` are compiled only under `#if WITH_EDITOR`, so no packaged target has the variable at all | lines 41–58 |
| `Source\RockyKombat\Arena\RKCArena.cpp` | `GreyboxRingRequested()` returns `false` in every non-editor build | lines 60–70 |
| `Source\RockyKombat\Arena\RKCArena.cpp` | `GreyboxFallbackAllowed()` returns `false` in every non-editor build, so a failed image import can never present a greybox ring as the arena | lines 72–84 |
| `Source\RockyKombat\Arena\RKCArena.cpp` | `SetGreyboxVisible(true)` is **refused** in a packaged build and logged as an error, whatever asked for it | lines 383–400 |
| `Source\RockyKombat\Arena\RKCArena.cpp` | `GetImageAspect()` reports the image's own width/height ratio, which the picture is always scaled by | lines 363–369 |
| `Source\RockyKombat\Arena\RKCArena.h` | Declaration and documentation of the editor-only policy | lines 18–25, 99–103 |

Unchanged and still true:

- Nothing is deleted. The procedural pieces stay in the world hidden by a visibility
  flag, and they never carried collision in the first place — `RKCVisuals::AddShape()`
  creates every one of them with `ECollisionEnabled::NoCollision`. The boundaries the
  fighters respect are `FMatchRules.ArenaMinX/ArenaMaxX` inside `RKC::FCombatSim`, and
  they are untouched, so no match result can change.
- One ring actor per world: `ARKCArena::BuildRing()` is idempotent (`if (bBuilt)
  return;`), so a rebuild, a reconfigure or a rematch cannot double up the geometry.
- The picture is placed and scaled by `ApplyFraming()`: its painted floor line is
  projected onto exactly the screen line the fighters' feet project onto, and it is
  then scaled until it covers the whole frame at its own depth with a 5 % overscan
  (`FramingOverscan`). The picture is never distorted — width and height are both
  derived from the single number `ArenaImageAspect` (1672 × 941 px).
- Windows narrower than 4:3 (a phone held upright, a square window) are rendered as a
  4:3 view with bars rather than a tall thin one, and the picture is framed to cover
  that 4:3 view; `UpdateArenaFraming()` re-frames on every change of window shape, and
  logs both the window aspect and the rendered aspect.

## 3. Why editor-only rather than "off by default"

Gating on editor-only is the only gate that matches the requirement. `WITH_EDITOR` is
`0` in every Game target UBT builds, Development and Shipping alike, so:

- a packaged build contains no switch to hide, therefore no launch command and no
  console setting is needed (requirement 1);
- no value saved in `Engine.ini`, no `-rkc.ArenaGreybox=1` on the command line and no
  future call site can re-show it, because the code path is not in the binary
  (requirement 2 — greybox is development-only and off during normal play);
- the greybox presentation is still available where it is useful: in the editor, for
  comparing the procedural geometry against the artwork, by setting
  `rkc.ArenaGreybox=1` there.

## 4. What the automated self-test now asserts

`ARKCGameMode::RunSelfTestMatches()` gained checks that make "one visible arena" a
number instead of an impression. They read the **components' own** visibility flags
(`CountVisibleGreyboxPieces()`), not the arena's internal intention.

| Check | Where | What it proves |
| --- | --- | --- |
| exactly one ring actor in the world | `RKCGameMode.cpp` ≈2675–2690 | no duplicate spawned ring actor, including one a visibility flag would hide |
| 0 of N greybox pieces visible | ≈2737–2740 | nothing is drawn around the imported image |
| the ring reports its greybox hidden | ≈2741–2742 | the actor's own state agrees |
| picture aspect == image aspect | ≈2743–2746 | the image was not stretched to fit the window |
| feet/floor screen delta ≤ 0.001 | ≈2747–2750 | the fighters stand on the image's painted floor line |
| narrowest cover margin > 0 cm | ≈2751–2754 | no background, floor or placeholder shows beside the image |
| per restart: 0 visible pieces **and** unchanged piece count | ≈2798–2815 | a rematch neither re-shows nor rebuilds a second ring |
| per restart: the imported image is still the arena | ≈2813–2814 | the rematch path did not fall back |
| end of run: same two conditions | ≈2940–2954 | the state a player is left in after results and rematch |

With the default 3 matches this adds 14 checks (1 + 5 + 3×2 + 2). The failing check's
own message contains the count, so a regression is readable from the record alone.

## 5. How to verify — the only thing that closes this item

Terminal execution was unavailable while these edits were made (the environment refused
every command), so **nothing below has been run for this change yet**. The previously
packaged `RockyKombat.exe` predates it. Run these in order.

### 5.1 Rebuild and repackage

```bat
cmd.exe /c Scripts\build_editor.bat > build_editor.log 2>&1
cmd.exe /c Scripts\package_win64.bat > packaged.log 2>&1
```

Success: `UnrealEditor-RockyKombat.dll` rebuilt, then `BUILD SUCCESSFUL, ExitCode 0`
and a fresh `Packaged\Windows\RockyKombat.exe` timestamp.

### 5.2 Automated single-arena evidence, from the packaged binary

```bat
Packaged\Windows\RockyKombat.exe -windowed -ResX=1280 -ResY=720 -RKCSelfTest=3 ^
    > packaged_selftest_arena.log 2>&1
```

Read `Saved\Evidence\RKC_SELFTEST_<timestamp>.txt`:

- `failed checks: 0` / `result: PASS`;
- the `--- IMPORTED ARTWORK (present) ---` block shows `0 of N greybox piece(s)
  visible` and a cover margin greater than 0;
- every `match N restart:` line PASSes.

### 5.3 Visible evidence, at every stage, including a rematch

```bat
cmd.exe /c Scripts\capture_stages.bat 3 C:\RockyKombat\capture_stages.log
python Tools\Diagnostics\verify_stage_shots.py Saved\Evidence\Stages\<timestamp>
```

Then, for the rematch case the request calls out:

```bat
Packaged\Windows\RockyKombat.exe -windowed -ResX=1280 -ResY=720 -log -stdout
```

Play a round to the results screen, press `R` (or `rkc.Rematch`), and screenshot the
second round start and mid-round. Also re-check after a knockdown and during the
introductions.

The arena's own log is the check that does not need an eye: on every re-frame it prints

```
Arena framing: aspect ..., FOV ... -> picture W x H cm at Y ...; frame Z [.., ..] and
painted floor line Z .. ; feet at +.... of the half-frame, picture floor line at +....
(delta 0.00000); narrowest cover margin ... cm.
```

- `delta 0.00000` — feet on the painted floor line.
- `narrowest cover margin` **positive** — no empty space above, below or beside the
  picture.
- any `Arena: something asked for the procedural greybox ring in a packaged build`
  error line means this fix is not holding and must be reported as a failure.

### 5.4 Aspect ratios to check

Repeat 5.3 with `-ResX/-ResY` at `1920x1080`, `1280x720`, `1024x768` and a portrait
`1080x1920`. Expected: the picture covers the frame in all four, the fighters' feet
stay on the painted floor, and the portrait window renders the 4:3 view with bars
beside it rather than a stretched arena.

### 5.5 Screenshots to supply back

At minimum: round start and mid-round, each showing the complete arena, both fighters
and the HUD, plus one post-rematch frame. Compare against the supplied image.

## 6. If a second ring is still visible

The duplicate can only come from these places now. In order:

1. **A stale packaged build.** `Packaged\Windows\RockyKombat.exe` and
   `Packaged\Windows\RockyKombat\Content\Paks\*.pak` must both have the timestamp of
   the run in 5.1. An old exe with a new pak, or the reverse, shows the old code.
2. **Stale intermediates.** Delete `Intermediate\Build` and `Saved\Cooked` (or run the
   package script with a clean stage) and rebuild if the new code is not in the binary.
3. **A second arena actor.** The self-test's "exactly one ring actor" check fails if
   the world holds two. `Config\DefaultEngine.ini` points the default map at
   `/Engine/Maps/Entry` and the ring is built at runtime in `StartPlay()`, so a second
   actor would mean a stale `.umap` being loaded (check for any extra map in
   `Content`) or a second spawn call site.
4. **A saved console override.** Even though the variable no longer exists in a
   packaged build, check `Saved\Config\Windows\Engine.ini` for a lingering
   `[ConsoleVariables]` entry and delete it; the self-test log prints the switch
   override table the game module applied at startup.
5. **Art not resolving.** If the arena image cannot be loaded, the picture is absent —
   and with this change nothing replaces it, so the background is empty rather than a
   second ring. `Arena: the imported image is not up yet; retrying` in the log means the
   import, not the visibility policy, is the problem: re-run the import
   (`Scripts\import_art.py`) and repackage so the art is in the pak.

## 7. Status of this item

| Item | State |
| --- | --- |
| Duplicate procedural ring removal — compile-time gate | **Implemented in source** (editor-only); not compiled |
| Defence-in-depth refusal in packaged builds | **Implemented in source**; not compiled |
| Image aspect ratio, cover framing, feet-on-painted-floor | **Implemented in source**; framing is arithmetic and self-logging |
| Self-test assertions for one visible arena at every stage | **Implemented in source**; not yet run |
| Rebuild + repackage | **Not done** — terminal execution unavailable |
| Packaged playthrough with screenshots at round start, gameplay and rematch | **Not done** — requires a person at a built executable |
| Human acceptance of the arena against the supplied image | **Not done** |
