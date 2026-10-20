# ROCKY KOMBAT — documentation index

Project root: `C:\RockyKombat` · Engine: Unreal Engine 5.8.3 (`C:\Program Files\Epic Games\UE_5.8`)

| Document | What it covers |
| --- | --- |
| [01-environment.md](01-environment.md) | Machine, toolchain, engine install, blockers |
| [02-build-and-run.md](02-build-and-run.md) | Every build and run command, plus evidence locations |
| [03-architecture.md](03-architecture.md) | Module layout, rules/presentation split, console interface |
| [04-evidence.md](04-evidence.md) | What has actually been observed, with commands and results |
| [05-requirements-checklist.md](05-requirements-checklist.md) | Product requirements against current status |
| [06-known-issues-and-remaining-work.md](06-known-issues-and-remaining-work.md) | Known issues, open risks, next concrete steps |
| [07-delivery-report.md](07-delivery-report.md) | Gate report: run the build, controls, and automated vs rendering vs human testing |
| [08-playtest-checklist.md](08-playtest-checklist.md) | Human acceptance checklist for a packaged build |
| [09-arena-single-ring.md](09-arena-single-ring.md) | The supplied image as the only visible arena: the fix, the assertions, and the packaged-build verification that closes it |

## Status snapshot (2026-10-20)

**Working, with evidence**

- Deterministic combat core (`RKC::FCombatSim`) in pure C++, builds outside Unreal. 364 rule checks, 0 failures.
- Offline AI (`RKC::FBotBrain`), pure C++, no engine dependency. 224 checks, 0 failures, including a cross-check against `RunBotMatch`.
- Unreal module `RockyKombat` compiles clean for the editor target; `UnrealEditor-RockyKombat.dll` is produced.
- The match director builds its world procedurally (26 ring pieces, 2 fighter rigs of 7 parts, camera), runs full bot-versus-bot matches, and writes evidence records. Fighter identity is shown by the HUD scoreboard, not by world-space text: a floor-level nameplate component used to draw black-on-black lettering under each fighter and was removed on 2026-10-20 (see [06](06-known-issues-and-remaining-work.md) section 6).
- In-engine self-test: 3 matches, 3 587 simulated ticks, 0 failed checks, engine-versus-standalone agreement `YES`, deterministic repeat.
- **Packaging works (gate A):** `Scripts\package_win64.bat` produced `Packaged\Windows\RockyKombat.exe` (BUILD SUCCESSFUL, ExitCode 0, 0 errors, 0 warnings). The VC++ redistributable blocker is resolved. The packaged binary passes the same self-test with the same match checksums as the editor build. See [04](04-evidence.md) D2 and [06](06-known-issues-and-remaining-work.md) K7.
- **The supplied artwork is imported and renders.** 64 sprites, 17 flipbooks, 3 materials and one arena texture are in the pak; a windowed packaged frame shows both fighters, the HUD and the arena. See [04](04-evidence.md) D2.1 and [03](03-architecture.md).
- **One arena, from the shipped binary.** The packaged build asserts a single visible ring at every stage including rematches (`0 of 27 greybox piece(s) visible`), and its stage captures were pixel-verified 7/7. See [09](09-arena-single-ring.md) section 5.

**Not yet done**
- A person has driven the shipped build through the front end into a match that ended in a knockout (`RKC_20261019_1913/1917/1803_match-end.txt`), but that playthrough landed no punches, so the jab, heavy punch, block and dodge **feel** is still unjudged by hand. The 8-point acceptance checklist in [08](08-playtest-checklist.md) is the remaining gate. HUD drawing is now asserted automatically and its stage captures were pixel-verified 7/7.
- Gate C is only partly closed: the art is integrated but has not been reviewed side-by-side against reference images, which were not supplied separately.
- The product brief PDF ("RockyHub Combat Game System.pdf") is still not on this machine, so brief sections 2–13 and the championship belt-transfer rules stay blocked. The artwork pack, by contrast, **was** received and read (see [05](05-requirements-checklist.md)).
- Mobile is game-side ready but engine-blocked: on-screen touch controls, the iOS/Android lifecycle guard and an options persistence save game are implemented and asserted on the shipped binary, but no APK has been produced because `Engine/Binaries/Android` is absent until the Android target platform is added through the Epic Games Launcher. There is no mobile device on this machine, no networking and no online services.
