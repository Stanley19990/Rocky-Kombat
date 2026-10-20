# 07 — Delivery report

Date: 2026-10-17. This is the gate report for the two things asked for last: a **runnable
Development build you can play a full match in**, and the **artwork connected to the tested
combat core**. It also separates the three kinds of testing so nothing is claimed at the wrong
strength.

## 1. Run it

```
C:\RockyKombat\Packaged\Windows\RockyKombat.exe -windowed -ResX=1280 -ResY=720
```

Or, from the project root:

```bat
Packaged\Windows\RockyKombat.exe -windowed -ResX=1280 -ResY=720
```

- Executable: `C:\RockyKombat\Packaged\Windows\RockyKombat.exe` (Development configuration).
- Drop `-windowed -ResX=1280 -ResY=720` to run full-screen at the desktop resolution.
- The match starts immediately — there is no map or menu to pick, because the world is built in
  code (`Config\DefaultEngine.ini` points the default map at `/Engine/Maps/Entry`).
- Add `-RKCSelfTest=2` to that same command line to make the shipped binary run the automated
  self-test and exit instead of playing.

### Controls

| Action | Player 1 (ROCKY, side A) | Player 2 (SHADOW, side B) |
| --- | --- | --- |
| Move left / right | `A` / `D` | `Left` / `Right` |
| Block (hold) | `S` | `Down` |
| Jab (light) | `J` | `NumPad 1` |
| Heavy | `K` | `NumPad 2` |
| Dodge | `L` | `NumPad 3` |

Shared: `R` rematch · `F1` frame-data overlay · `Escape` quit.

By default `rkc.BotSide = 1`, so the **bot plays SHADOW and you play ROCKY**. Set `rkc.BotSide=0`
in the console for two humans on one keyboard.

## 2. What was verified, by test kind

### 2.1 Automated tests (no human involved, pass/fail from logs)

| Test | Result | Evidence |
| --- | --- | --- |
| Combat rules, plain MSVC, no engine | 364 checks, 0 failed | 04.A |
| Offline AI, `/W4 /WX`, no engine | 224 checks, 0 failed | 04.B |
| Editor module build | `Result: Succeeded` | 04.C |
| In-engine self-test, editor build | 3 matches, 0 failed checks, cross-check `YES` | 04.D |
| **Packaged binary self-test** | exit 0, 0 failed checks, same checksums as the editor build | 04.D2 |
| Packaged artwork resolution | all 8 animations resolved for both fighters, arena backdrop present | 04.D2 |
| Runtime input binding in the packaged build | `Player input bound (scheme=hot-seat).`, no missing-component error | 04.D2.1 |

### 2.2 Visible rendering tests (a human or a script looked at real pixels on this host)

| Check | Result | Evidence |
| --- | --- | --- |
| Packaged build renders windowed on this host | yes — earlier stall did not reproduce | 04.D2.1 |
| Both fighters drawn with imported sprites | yes | `Saved\Evidence\packaged_shot_early.png`, `packaged_shot_clinch.png` |
| HUD drawn (panels, round counter, timer, event feed, control footer) | yes | same frames |
| Fighters apart at round start | yes | `packaged_shot_early.png` (start positions 600 cm apart) |
| `ForwardShadingPriority` light warning | absent from the packaged frame | 04.D2.1, K8 |
| HUD draws on **every** presentation screen | yes — 7 of 7 stages, each with the HUD drawing | `Saved\Evidence\Stages\20261017_235858\` (startup, loading, menu, versus, match, touch pad, results) + `manifest.txt` |
| HUD counters asserted with a real renderer | yes — `drawn frames 5, layout regions 6, canvas 1280x720`, 167 checks / 0 failures | `Saved\Evidence\RKC_SELFTEST_20261018_000537.txt` |
| Screenshot pixels checked independently of the game | yes — 7 of 7 pass size / colour variety / near-black / HUD-text-and-accent checks | `Tools\Diagnostics\verify_stage_shots.py`, `pixel_verification.json` |

The first five rows were inspected **by eye**. The last three are automated, and one caveat applies to
them: the pixel check proves each frame is a real, HUD-bearing 1280x720 image, while *which* screen it
is comes from the game's own manifest — and the stage frames were taken with the touch pad forced on
(`rkc.TouchForced=1`), so they are not a normal run's clean menus. Tracked as K6 and 04.D4.

### 2.3 Human playtests

**None has been performed.** No person has pressed a key yet. Everything up to the keystroke is
covered automatically (bindings registered, bot input proven to travel the same `FInputState`
channel), but "I played a round and the keys responded" has not happened on this machine and is
not claimed. This is remaining work item 3 in 06.

**Correction, 2026-10-19.** An earlier draft of this report described the 19:13–19:18 records on the
18:34 build as an interactive human playthrough. That was wrong and is retracted here. The record
`RKC_20261019_191803_match-end.txt` reports 20 screen changes with **0 accepts, 0 taps and 0 option
changes** — the front end moved on its own scripted driver, which calls `ActivateItem` directly and
never increments the accept counter — and the launch log for that run
(`Saved\Logs\RockyKombat-backup-2026.10.19-18.18.03.log` line 297) shows it started with `-unattended
-rkc.FrontEndAuto=3 -rkc.TouchForced=1 -rkc.CaptureStages=1`. It was an unattended capture run:
side A produced 0 attacks. The whole evidence folder now has 28 match records, of which 24 (every
record from 18 October onwards) show the same two signatures, and 2 of the 4 older 17 October
records show player-side input on that earlier build (2 attacks/1 hit/3 dodges, and 1 attack).
The correction is carried into the pack itself: `KNOWN_ISSUES.txt`, `START_HERE.txt` and the new
excerpt `RKC_20261019_191803_launch-log-excerpt.txt` in the pack's evidence folder all state it.

## 3. Artwork vs the supplied specification

Read from `START_HERE.txt` / `AI_IMPORT_PROMPT.txt` and checked against the import.

| Pack says | We did | Match |
| --- | --- | --- |
| 64 frames, 384×320, pivot (192, 300) from top-left | 64 sprites imported at 384×320, custom pivot retained | yes |
| Two 1536×2560 atlases, 4 col × 8 row | imported from the separated PNGs (no approximate slicing) | yes |
| One 1672×941 arena, single flattened image | imported as one background; fighters/HUD in front | yes |
| Rows Idle/Walk/Jab/HeavyPunch/Block/Dodge/Knockdown/Victory, 4 frames each | 8 flipbooks per fighter in that order | yes |
| Keep shared combat code across fighters | Rocky and Shadow run the same `FCombatSim`; only art differs | yes |
| Don't recompute pivot per silhouette (jitter) | fixed pivot used | yes |
| Shadow's Dodge third pose reads as falling — review before use | safe subset used (`FB_Shadow_Dodge_Safe`) | yes, substitution recorded |
| No HitReaction / GetUp art | knockdown's first pose is a temporary hit stand-in, labelled as such | documented gap |
| Don't rig these as 3D models | Paper2D sprites/flipbooks used, not skeletal meshes | yes |

### Discrepancies to be aware of

- **The pack is an art pack, not a game.** It contains no `.uasset`, rig, sound or gameplay code,
  and it explicitly states it did not validate engine playback, materials, hitboxes, timing or
  device performance. Those all had to be built and tested here (they were).
- **No reference images were supplied separately**, so gate C's "reviewed against references" half
  is still open; only the import itself is verified.
- **The combat PDF is still not on this machine.** It was searched for again
  (`*rocky*combat*.pdf`, `*combat*game*system*.pdf`, `*rocky*.pdf` across Downloads, Desktop,
  Documents and the project tree) and returned nothing. Every requirement that only the PDF can
  define — brief sections 2–13, and the championship belt-transfer rules — stays `BLOCKED`, and no
  requirement has been invented to fill the gap. The artwork pack's requirements, by contrast, are
  now unblocked and implemented.
- The self-test reports 26 ring pieces in the greybox editor run and 27 in the packaged run that
  carries the imported backdrop. The extra piece is consistent with the backdrop actor, but it was
  not isolated by a dedicated test.

## 4. Where this leaves the gates

| Gate | State |
| --- | --- |
| A — compiles, runs, packages | **DONE** (packaged binary predates the newest code — rebuild before shipping) |
| B — two fighters complete a reliable offline match | DONE headless and windowed; the HUD draws on all seven presentation screens with a pixel check. Human-input round still open (no person has pressed a key) |
| C — art integrated and reviewed | PARTIAL — integrated and rendering; reference review open, no references supplied |
| D–H | untouched (no device, no networking, no services, no persistence) |

## 5. Next concrete steps

1. Play one round with the command in §1 and report which keys (if any) did not respond — that
   closes the last gap in gate B, which no automated run can close (04.E.3).
2. Rebuild the packaged Win64 build: the shipped archive was staged before the front end, audio,
   touch controls and the stage-capture code were added (`Scripts\package_win64.bat`), then re-run the
   packaged self-test.
3. Supply the combat PDF so brief sections 2–13 and the belt-transfer rules can be implemented
   rather than left blocked.
4. Optionally supply reference images, which would let the gate C review be completed.


## 6. Distribution pack — 2026-10-19

A player-facing pack now exists and has been verified by unpacking it, not by copying it. It was
rebuilt once more after the "human playthrough" correction, so that the corrected player notes and
the launch-log excerpt that backs them travel inside the same archive.

- `C:\RockyKombat\Dist\RockyKombat-Win64-1.0.0\` — the game, 181 files, 1,001,126,513 bytes, plus
  `START_HERE.txt` (run instructions, controls, a 60-second first round, the evidence list),
  `KNOWN_ISSUES.txt` (what is unverified, what to watch for, what is design rather than a defect),
  `FIRST_ROUND_GUIDE.md` with the seven real screens in `Screens\` and the provenance manifest that
  ships beside them, and `SHA256SUMS.txt` (every file).
- `C:\RockyKombat\Dist\RockyKombat-Win64-1.0.0.zip` — 452,883,760 bytes (432 MiB),
  sha256 `d4c67c861ff29890141a1a7fb9ae9ab16d108bba6acdf8ebe9ac417e0da420ca`.
- Verification of that exact zip, done on the extract rather than on the copy step: 181 files and
  1,001,126,513 bytes unpacked in 8.9 s, `sha256sum -c` against the packed manifest returned
  **180 OK / 0 FAILED**, the guide checker inside the extract passed **9 of 9**, and the extracted
  copy ran its own self-test to **219 checks / 0 failed**. The record is kept as
  `RKC_SELFTEST_20261019_220040_extracted-from-zip.txt` (and the previous repack's as
  `RKC_SELFTEST_20261019_214847_extracted-from-zip.txt`). The corrected notes are provably the ones
  inside this archive: the packed manifest carries `START_HERE.txt` as
  `38977343389f70ac…` and `KNOWN_ISSUES.txt` as `8c5479e012a34cf5…`, the edited files' own hashes.
  Full detail, including the three earlier archives this one supersedes (`435,898,643` bytes, then
  `445,228,907`, then `452,873,834`): `docs\13-distribution-pack.md`.

This section carried the 19:36 archive's figures (`164` hashes, `200` checks) and then the 21:50
repack's (`178` hashes, 219 checks) before this rebuild; those numbers are kept as history in
`docs\04` section F and are no longer the current ones.

Because the staged files are byte-identical to the package every earlier run used, the package-level
evidence transfers to the pack: packaged headless self-test 219/0, packaged windowed self-test 222/0
with the HUD draw asserted, 7-of-7 stage capture from the shipped binary, and a scripted
front-end-to-ring run that reached a knockout and returned to the menu — scripted, not played: that
run was started `-unattended -rkc.FrontEndAuto=3`, and its record counts 0 accepts (see section 2.3
for the correction and `docs\06` remaining work 3 for the evidence). No human playthrough exists on
this build yet.

What the pack still cannot claim: nobody has ticked off the 8-point human acceptance checklist, and
no one has confirmed by ear that the audio is audible. Both are in `KNOWN_ISSUES.txt` in the same
words, so a player meets the limitation before the limitation meets them.

Android remains the one outstanding platform, and it is one launcher click plus a 4.5 GB download
away from being attempted: `docs\12-safearea-and-android-status.md` section 4 has the exact steps and
the re-validated evidence (`Android VALID r27c` from UBT; the missing
`Engine\Binaries\Android\UnrealGame.target` marker named by `Engine\Config\BaseEngine.ini`).


---

## Increment 2026-10-19 20:00 — robustness and evidence integrity, repackaged

The Windows deliverable was rebuilt, repackaged and re-verified. This increment changed
diagnostics and lifecycle accounting, not gameplay: the cooked pak is byte-identical to the
previous package, and only `RockyKombat\Binaries\Win64\RockyKombat.exe` differs. Full manifest
and hashes: `docs\13-distribution-pack.md`.

### What was built

| Step | Result |
| --- | --- |
| Editor build (4 TUs changed, 2 compile errors fixed) | `Result: Succeeded`, `UnrealEditor-RockyKombat.dll` relinked |
| Editor headless self-test | `RKC_SELFTEST_20261019_200301.txt` — 219 checks, 0 failed |
| Win64 package (build + cook + stage + pak + archive) | `BUILD SUCCESSFUL`, 321 s, exe staged 20:07 |
| Packaged headless self-test | `RKC_SELFTEST_20261019_200936.txt` — 219 checks, 0 failed |
| Packaged windowed self-test (HUD draw asserted) | `RKC_SELFTEST_20261019_200954.txt` — 222 checks, 0 failed, 5 draw passes / 6 regions / 1280x720 |
| Stage capture of the shipped binary | `Saved\Evidence\Stages\20261019_201119\` — 7 of 7 stages, HUD drawing on all 7; independent pixel check 7 of 7 PASS |
| Distribution pack | `Dist\RockyKombat-Win64-1.0.0\` — 154 files, 949 MB; zip 445,228,907 bytes, sha256 `91b9e6f7…c85278` |
| Archive verification | extracted with .NET ZipFile (154 files, 10.1 s), `sha256sum -c` 153 OK / 0 FAILED, extracted copy's own self-test 219 checks / 0 failed, exit 0 |

### What it added

1. **The self-test can no longer pass with the artwork missing.** A cooked build must carry
   the art; the check is now in the record instead of the block being skipped silently.
2. **Application lifecycle is asserted.** 14 new checks simulate a real background/return with
   a held guard, movement and a pending punch plus two fingers on the on-screen pad, then prove
   that no input survives, that the music is frozen rather than stopped or restarted, that the
   audio layer records exactly one freeze and one resume request, and that the first input the
   simulation reads after the return is neutral.
3. **The fighter material question is answered from inside the running game,** not from the
   asset list: both fighters shade with `/Paper2D/MaskedUnlitSpriteMaterial`, which is why
   turning around or losing focus cannot darken them, and neither fighter casts a shadow.

### What it did not change

No combat, presentation, audio-mixing or content change. The pak hash is unchanged, which is
the mechanical proof of that claim. The two things the previous report listed as the remaining
honesty gaps are unchanged too: nobody has ticked off the 8-point human checklist, and nobody
has confirmed the audio by ear. Both are still stated in `KNOWN_ISSUES.txt` inside the pack.
