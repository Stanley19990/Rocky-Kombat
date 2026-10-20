# 01 — Environment

## Machine

| Item | Value | Source |
| --- | --- | --- |
| Host | `DESKTOP-47U8R99` | runtime |
| OS | Windows 10 22H2, build 10.0.19045.2965 | engine log metadata |
| CPU | Intel Core i7-8650U @ 1.90 GHz, 4 physical / 8 logical cores | engine log metadata |
| Memory | 7.86 GB physical | engine log metadata |
| GPU | Intel UHD Graphics 620 (integrated) | host audit |

This is a low-end development laptop. Two consequences are visible in every log:

- UnrealBuildTool reports *"Requested 1.5 GB memory per action, 2.04 GB available: limiting max parallel actions to 1"* — the editor module is effectively built one translation unit at a time.
- A full editor initialisation takes roughly 60–75 s before any gameplay runs, most of it asset-registry discovery (35.7 s on the run recorded in `Saved/Logs/RockyKombat.log`).

## Toolchain

| Item | Value |
| --- | --- |
| Unreal Engine | 5.8.3-58210709+++UE5+Release-5.8 |
| Engine path | `C:\Program Files\Epic Games\UE_5.8` |
| UnrealBuildTool | bundled, driven through `Engine\Build\BatchFiles\Build.bat` |
| AutomationTool | `Engine\Build\BatchFiles\RunUAT.bat` |
| Visual Studio | 2022 Community, MSVC 14.44.35207 |
| Windows SDK | 10.0.22621.0 |
| ISPC | bundled 1.24.0 |
| Android SDK / NDK | **not installed** |

## Blockers that shape the plan

1. **No Android SDK or NDK.** Any on-device work (acceptance gate D, "controls and performance are tested on an agreed mobile device") cannot start until those are installed and a device is available, and the free space cost is significant. Windows-only milestones are therefore the correct order of work for now.

2. **Product brief PDF never arrived.** The instruction to read "RockyHub Combat Game System.pdf" could not be followed: no PDF exists on the machine outside the engine's own documentation set, and no file was supplied. Every requirement that would come from that document is marked `BLOCKED` in 05 rather than guessed at. The requirements captured in 05 come only from the written instructions.

3. **Character and arena reference images never arrived.** `C:\RockyKombat\ArtSource\` was created for them and is empty. Gate C ("character and arena assets are integrated and reviewed") cannot start, so the match is greybox: the fighters and the ring are built from engine primitives at runtime.

4. **No discrete GPU.** A rendering run (`-game` without `-nullrhi`) stalls during graphics initialisation on this host and never reaches gameplay; see 06. All automated verification therefore runs headless, and HUD pixel output is not yet verified here.

## Disk

The free space that the project has available (measured during this session):

- `C:` — 87 GB free after packaging work began.
- An earlier step recovered 108 GB by removing a stale 100 GB Linux engine build (`Linux_Unreal_Engine_5.8.3`, 300 132 files) that was left in `C:\Users\DELL\Downloads`.
