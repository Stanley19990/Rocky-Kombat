# 05 — Requirements checklist

Legend: **DONE** · **PARTIAL** (some of it works, with evidence in 04) · **NOT STARTED** · **BLOCKED**

## Sources actually in hand

| Source | State | Notes |
| --- | --- | --- |
| Written project instructions | received in part | The transmitted sections drive this checklist. Sections cited but never transmitted are marked `BLOCKED — text not received`. |
| Artwork pack `Rocky_Kombat_Asset_Pack.zip` | **received and read** | `C:\Users\DELL\Downloads\Rocky_Kombat_Asset_Pack.zip`, extracted at `C:\Users\DELL\Downloads\Rocky_Kombat_Asset_Pack\Rocky_Kombat_Asset_Pack\`. `START_HERE.txt`, `AI_IMPORT_PROMPT.txt`, `asset_manifest.json`, `CHECKSUMS.json` and `QA_REPORT.json` were opened and read, and the pack was imported into `Content\RockyKombat\Art` (64 textures, 64 sprites, 17 flipbooks, 3 materials, 1 arena texture; 0 errors — 04.C). |
| Combat PDF "RockyHub Combat Game System.pdf" | **still absent** | Re-searched 2026-10-17 across `Downloads`, `Desktop`, `Documents` and the project tree for `*rocky*combat*.pdf`, `*combat*game*system*.pdf` and `*rocky*.pdf`: no paths returned. Requirements only the PDF can define (brief sections 2–13, championship belt-transfer rules) stay `BLOCKED — source missing`. The exact reportable error is "not found on this machine"; no requirement has been invented to fill the gap. |

`source missing` now applies **only** to the combat PDF. The artwork pack's requirements are unblocked and implemented; that part of K5 in `06` is resolved.

## What the artwork pack actually specifies

Read from `START_HERE.txt` and `AI_IMPORT_PROMPT.txt` in the extracted pack:

- **Contents.** 64 individual RGBA PNG frames (32 per fighter), two atlases, one empty rooftop arena, 16 GIF previews, a browser preview, descriptive JSON frame maps, a QA report and an AI integration prompt. Two fighters: Rocky and Shadow; one stage: Midnight Rooftop Ring.
- **Geometry.** Each individual frame is 384 × 320 px with a bottom-centre custom pivot at (192, 300) in image coordinates measured from the top-left. Each atlas is 1536 × 2560 px, a regular 4-column × 8-row grid. The arena is a single 1672 × 941 px flattened image. The pack's JSON is descriptive and is **not** an Unreal import descriptor, so integration used the already-separated PNGs rather than approximate slicing.
- **Rows (actions).** Idle / Walk / Jab / HeavyPunch / Block / Dodge / Knockdown / Victory, four frames per row, left to right, artwork initially facing right.
- **Modes.** Rocky Kombat has three modes with separate rule sets: **RBA** (boxing rules), **Street Fights**, and **Rocky Universe**. RBA rules are boxing-only; Shadow's martial-arts outfit does **not** authorise kicks or special attacks inside RBA, and a mode-approved RBA glove/costume variant may be required before release. The supplied roster is a shared art-preview roster, not a mode-final one.
- **Offline / casual / competitive separation.** Offline and casual play must not affect ranked records. Pay-to-win, ads, mandatory crypto, player stakes and client-authoritative competitive outcomes are all prohibited.
- **Explicitly out of scope for the pack.** No `.uasset`, rig, skeletal mesh, sound, gameplay code or finished game is included. Engine playback, materials, device performance, hitboxes and combat timing were **not** validated by the pack; those remain this project's job and are covered by 04.
- **Championship belt-transfer rules.** Not stated anywhere in the pack. They can only come from the combat PDF, which is absent, so they stay `BLOCKED — source missing`.

## Documented artwork limitations (from the pack, carried into the import)

- Technically prepared and importable, but **not** a polished production animation set: four key poses per action can look choppy, pose continuity varies, generated outlines can carry minor artifacts, and idle variation is limited.
- Shadow's **Dodge** contains a low fallen pose that must be reviewed before use as an evade; a safe subset flipbook (`FB_Shadow_Dodge_Safe`) was created and the substitution recorded rather than hiding the defect.
- There is **no** dedicated HitReaction or GetUp sequence. Knockdown's first pose can stand in as a temporary hit response during prototyping; it is not a finished replacement.
- The arena is one flattened image, not parallax layers or 3D geometry. Character lighting is baked into the sprites, and scaling sprites much above their ~200-px standing height softens the artwork.

## Product framing

| # | Requirement | Status | Notes |
| --- | --- | --- | --- |
| P1 | Build Rocky Kombat as an original combat product inside the RockyCat Ecosystem | PARTIAL | Original code and content. Ecosystem integration is not started. |
| P2 | Connect to RockyHub through shared player services | NOT STARTED | Needs service contracts, credentials and endpoints, none of which exist yet. |
| P3 | Support a standalone release | PARTIAL | The project runs standalone and now packages: `Packaged\Windows\RockyKombat.exe` (04.C, 04.E.2). |
| P4 | Build only Rocky Kombat first (not the whole ecosystem) | DONE | Only this game's module exists. |

## Product brief sections 2–13

| # | Requirement | Status |
| --- | --- | --- |
| B1 | Sections 2 through 13 of the brief (combat detail, modes, economy, UX, etc.) | BLOCKED — source missing. The text of these sections was not received and the PDF is not on this machine; no implementation claim is made for any of them. The pack's mode names (RBA / Street Fights / Rocky Universe) and its offline-vs-ranked constraint are used, but the brief's detail is not. |

## RBA match requirements

| # | Requirement | Status | Notes |
| --- | --- | --- | --- |
| R1 | Two placeholder fighters complete a reliable offline match | DONE | In-engine self-test: 3 matches, all decided, 0 failed checks (04.D). Now also passes from the packaged binary. |
| R2 | Deterministic, framerate-independent rules | DONE | Fixed 1/60 s ticks; repeat run reproduced checksum `0x28F2ACC7` exactly. |
| R3 | RBA weight divisions expressed as data, not duplicated code | DONE | `RKC::MakeFighterConfig`; both launch fighters share one combat core. |
| R4 | Ring/arena bounds and anti-overlap separation enforced | DONE | Worst violation 0.000000 cm across 3 587 ticks. `rkc.MinSeparation` exposes the floor (58 cm default, clamped 10..600). |
| R5 | Offline AI that never gains an unfair advantage | DONE | 224 AI checks, including configuration neutrality and input-path equivalence (04.B). |
| R6 | Champion-grade AI that is more disciplined, not stronger | DONE | 27 vs 10 defensive orders per match at equal damage (04.B). |
| R7 | Training instrumentation that cannot change an outcome | DONE | Additive-only assertion in the AI harness. |
| R8 | Local two-player controls | PARTIAL | Bindings exist and are configurable — P1 `A`/`D` move, `S` block, `J` jab, `K` heavy, `L` dodge; P2 arrows + NumPad 1–3; `R` rematch, `F1` frame data, `Escape` quit. No human has played a round yet (04.E.3). |
| R9 | HUD: fighter panels, round timer, event feed, frame data | PARTIAL | Implemented and its inputs verified in-engine; a rendered packaged frame shows it on screen, but no independent pixel assertion exists (04.E.1, K6). |

## Sections 14–16 of the instructions

| # | Requirement | Status | Notes |
| --- | --- | --- | --- |
| 14a | Cosmetic and progression direction without selling competitive power | NOT STARTED | No economy code exists, which is the strongest possible form of "no pay-to-win" for now. |
| 14b | Combat Coins and premium currency with defined purposes and trusted accounting | NOT STARTED | Requires a server-side ledger; deliberately deferred. |
| 14c | No launch ads | DONE | No ad code, no third-party SDKs. |
| 14d | No $RKC, wallet, stake or token purchase required to play or compete | DONE | No crypto, wallet or payment code exists anywhere in the project. |
| 14e | No automatic $RKC emission for fight wins | DONE | No emission logic exists. |
| 14f | Financial/stakes services isolated and disabled, outside the initial build | DONE | Nothing financial is implemented or reachable. |
| 14g | Champion recognition: belts, badges, titles, entrances, cosmetics, frames, history | NOT STARTED | No progression or recognition system yet. Belt-transfer rules are also `BLOCKED` on the PDF. |
| 15a | Street Fights gets its own styles, kicks, combos, specials, environments, rules | NOT STARTED | Planned as a separate later milestone. |
| 15b | Street Fights must not inherit RBA divisions or championship belts | DONE (as a constraint) | Divisions are RBA-only and live behind `RKCRoster`; nothing forces shared rules. |
| 15c | Rocky Universe gets separate content and rules | NOT STARTED | Later milestone. |
| 15d | Reuse genuine shared systems, do not duplicate the project or force every mode into boxing rules | PARTIAL | The combat core, AI, evidence and HUD are mode-agnostic by construction; mode-specific content does not exist yet. |
| 16a | Maintain setup instructions, build steps, architecture notes, asset dependencies, test evidence, known issues, remaining work | DONE | This `docs\` set. |
| 16b | At each gate, report what works, what was tested, what is incomplete, and the next concrete step | DONE | Each gate report is in 04 and 06. |
| 16c | Do not equate a successful compile, an attractive menu or generated code with a finished game | DONE (as a working rule) | Every claim in 04 is tied to a log; 04.E lists the gaps. |

## Acceptance gates

| Gate | Definition | Status | Evidence |
| --- | --- | --- | --- |
| **A** | Project compiles, runs and packages | DONE | Compiles and runs (04.C, 04.D); `BuildCookRun` archived `Packaged\Windows\RockyKombat.exe` (BUILD SUCCESSFUL, ExitCode 0) and the packaged binary passed its own self-test with the same match checksums as the editor build (04.E.2). |
| **B** | Two placeholder fighters complete a reliable offline match | DONE (headless) | 3 complete matches in-engine, plus 1 535+ tick matches in the standalone harness. Human-input play and an independent HUD pixel check remain unverified (04.E.1, 04.E.3). |
| **C** | Character and arena assets integrated and reviewed | PARTIAL | The supplied pack is imported (64 sprites, 17 flipbooks, 1 arena texture) and survives the cook into the pak. Outstanding: a side-by-side review against reference images, which were not supplied separately. |
| **D** | Controls and performance tested on an agreed mobile device | BLOCKED | No Android SDK/NDK installed, no device agreed, no mobile rendering path configured. |
| **E** | Private casual matches survive realistic network conditions | NOT STARTED | No networking code. |
| **F** | Trusted identity, persistence and competitive validation work | NOT STARTED | No services integration. |
| **G** | Rankings and championships pass rule and failure-case tests | NOT STARTED | No ranking or championship system. |
| **H** | Later experiences implemented in separate milestones | NOT STARTED | Street Fights and Rocky Universe are untouched by design. |

## What to unblock next

1. Supply the **combat PDF** ("RockyHub Combat Game System.pdf"). This is the only remaining missing document; the artwork pack has been received and integrated. Separately, reference images to review the import against would help close gate C.
2. Confirm whether on-device work is in scope now, which requires installing the Android SDK/NDK (a large download) and naming the device.
3. Confirm the RockyHub service contract when ecosystem work (P2, F, G) is to begin.
