# 03 — Architecture

## The one rule that shapes everything

**`RKC::FCombatSim` is the truth. Everything else is a picture of it.**

The simulation, the AI and the roster are plain C++ inside namespace `RKC`. They include no Unreal headers, which is why they can be compiled and tested with nothing but MSVC, and why a rule can be verified on a laptop that cannot run the editor at speed. Unreal classes (`ARKCFighter`, `ARKCArena`, `ARKCHUD`, `ARKCGameMode`) only read simulation state and never write to it.

```
source tree (Source/RockyKombat)
├── Combat/
│   ├── RKCCombatCore.h/.cpp     RKC::FCombatSim, FFighterState, FMatchRules, FInputState,
│   │                            events, FBotMatchOutcome/RunBotMatch, DescribeMatchOutcome
│   └── RKCRoster.h/.cpp         the two launch fighters, fighter configs, rule presets
├── AI/
│   └── RKCAI.h/.cpp             RKC::FBotBrain, FBotProfile (archetype + difficulty), reflex model
├── Match/
│   ├── RKCGameMode.h/.cpp       match director: owns the live sim, builds the world, drives frames
│   └── RKCPlayerController.h/.cpp  two-player local input mapping
├── Fighters/RKCFighter.*        presentation of one side: follow the sim, draw sprite or greybox
├── Arena/RKCArena.*             ring geometry derived from the rules' arena bounds
├── Visual/RKCArt.*              imported Paper2D lookup: sprite/flipbook paths per action and side
├── Visual/RKCVisuals.*          shared drawing/primitive helpers
├── UI/RKCHUD.*                  canvas HUD: fighter panels, round timer, event feed, frame data
└── RockyKombat.h/.cpp           module, log category LogRockyKombat
```

## Fixed frame order in the director

`ARKCGameMode::Tick()` is a thin caller; the body lives in `AdvanceMatchFrame(float)`, which the self-test also calls so there is no second, test-only loop that could drift:

1. drain the human input latches (drained every frame even outside the fight, so a press made during the countdown cannot be replayed later)
2. gather one `FInputState` per side — a human controller, or `FBotBrain::Think()`
3. `Sim.Advance(DeltaSeconds, InA, InB)` — whole fixed 1/60 s ticks only, using an accumulator, so behaviour is framerate-independent
4. drain simulation events into the HUD event feed and the integrity counters
5. `SyncFromSim()` on both fighters — presentation is read **after** the rules have advanced, so a fighter can never be drawn in a state the rules are not in
6. update the centre banner from the match phase

## Rules expressed as data

`FMatchRules` carries rounds, round length, countdown, knockdown limit, arena bounds and `MinSeparation` (58 cm in the shipped preset). `RKC::MakeDefaultRules()` is 3 rounds of 30 s with a 3-knockdown rule; `RKC::MakeQuickRules()` is a single short round used by smoke runs. Divisions are expressed as `FFighterConfig` values, so both launch fighters run on the same documented code path.

## AI is offline-only and information-equal

`RKC::FBotBrain` sees exactly what a human sees through `FInputState`; it has no privileged channel into the simulation. Difficulty raises **reflexes and discipline only** — never damage, health, stamina or information. The standalone harness asserts this directly (config-neutrality, and an input-path equivalence check that proves record/replay of human input reproduces bot-driven matches).

## Imported art is wired to the tested core, not around it

The supplied pack was imported with Paper2D (64 sprites, 17 flipbooks, 3 materials, 1 arena texture) and then attached to the *existing* presentation layer. `RKCFighter` asks `RKCArt` for the sprite/flipbook that matches the current simulated action and side, and falls back to its greybox rig if a lookup fails, so a missing or failed import can never stop the simulation or the self-test. The artwork is reached through runtime **string paths** in `RKCArt.h` rather than hard asset references, which is why `+DirectoriesToAlwaysCook=(Path=\="/Game/RockyKombat/Art\")` in `Config\DefaultGame.ini` is needed to keep it in the pak; see 02 section 5 and K7 in 06. Nothing about the rule set, the AI or the evidence path changed when the art was added, and the packaged self-test confirms all eight animations per fighter resolve at runtime (04.D2).

Sprite geometry follows the pack's own contract: 384×320 canvases with a bottom-centre pivot at (192, 300), all eight rows in the order Idle / Walk / Jab / HeavyPunch / Block / Dodge / Knockdown / Victory. Shadow's Dodge uses the documented safe subset (`FB_Shadow_Dodge_Safe`) because the pack warns that its third pose reads as falling.

## World built in code, no map asset

`ARKCGameMode::BuildWorld()` constructs the ring, lighting, the camera and both fighter rigs procedurally. That keeps a clean checkout playable with zero binary content, and a hand-made `.umap` can replace `BuildWorld()` later without touching the simulation.

The self-test's reported ring-piece count reflects what was built: **26** in the greybox editor run (04.D) and **27** in the packaged run that has the imported art (04.D2). The extra piece is consistent with the imported arena backdrop being added to the world — the packaged record's "the arena presents the imported backdrop image" check is in the same run reporting 27 — but that was not isolated by a dedicated test, so treat the 26-vs-27 difference as observed with a plausible explanation rather than a proven one.

## Evidence as a first-class feature

Three layers exist purely so that claims can be checked:

- `Tools\CombatCoreTests` — 364 rule checks, plain MSVC, no engine.
- `Tools\AITests` — 224 AI checks including a `RunBotMatch` cross-check, built with `/W4 /WX`.
- `ARKCGameMode` — in-engine: every finished match can write `Saved\Evidence\RKC_*.txt`, and `CrossCheckAgainstStandalone()` replays the identical match through the pure-C++ harness inside the running engine process and compares outcomes. A green cross-check means the engine build and the standalone build really do implement the same rules, rather than that both merely compile.

`-RKCSelfTest=N` ties the three together, including a repeat-run determinism check and a HUD draw self-report.
